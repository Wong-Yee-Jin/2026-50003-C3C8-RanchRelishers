#include "render.h"
#include "assets.h"
#include <langinfo.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>

/* 80x24 is the classic terminal size, so anything at or above it gets the
   full wordmark; below that but still wide enough for readable lines drops
   to compact; anything narrower goes text only. */
render_mode_t render_mode_for(int cols, int rows) {
    if (cols >= 80 && rows >= 24) return RENDER_FULL;
    if (cols >= 40) return RENDER_COMPACT;
    return RENDER_MINIMAL;
}

/* Cursor visibility and screen switching are terminal state rather than
   styling, so they answer to the tty check on its own. Someone who exported
   NO_COLOR still wants their cursor back when the program exits. */
bool render_escapes_for(bool stdout_is_tty) {
    return stdout_is_tty;
}

/* Color needs a real terminal on the other end of stdout (a pipe or a log
   file has no use for escape codes), and NO_COLOR overrides even that, since
   a user who set it wants plain text regardless of what the terminal supports. */
bool render_color_for(bool stdout_is_tty, const char *no_color_env) {
    if (!render_escapes_for(stdout_is_tty)) return false;
    return no_color_env == NULL || no_color_env[0] == '\0';
}

/* There is no reliable way to ask a terminal what it can do. btop, cava and
   termbox2 all gave up and made this a setting instead, and termbox2 says so
   in its own header. We have no config file to put a setting in, so we read
   the two env vars every modern terminal sets and accept that guessing high
   costs us a slightly wrong shade, never garbage on screen. */
render_depth_t render_depth_for(bool stdout_is_tty, const char *no_color_env,
                                const char *colorterm_env, const char *term_env) {
    if (!render_color_for(stdout_is_tty, no_color_env)) return RENDER_COLOR_NONE;

    /* TERM=dumb is the documented way to say "no escape sequences here", and
       an unset TERM gives us no reason to believe any of them work. */
    if (term_env == NULL || term_env[0] == '\0') return RENDER_COLOR_NONE;
    if (strcmp(term_env, "dumb") == 0) return RENDER_COLOR_NONE;

    if (colorterm_env != NULL &&
        (strstr(colorterm_env, "truecolor") || strstr(colorterm_env, "24bit")))
        return RENDER_COLOR_TRUE;

    /* The -direct terminfo entries (xterm-direct, tmux-direct) carry the same
       meaning as COLORTERM, for terminals that set one but not the other. */
    if (strstr(term_env, "direct")) return RENDER_COLOR_TRUE;
    if (strstr(term_env, "256color")) return RENDER_COLOR_256;

    return RENDER_COLOR_16;
}

/* The live answer, for everything outside a unit test. */
render_depth_t render_depth(void) {
    return render_depth_for(isatty(STDOUT_FILENO), getenv("NO_COLOR"),
                            getenv("COLORTERM"), getenv("TERM"));
}

/* One row per meaning instead of one per color. The RGB triple drives the 256
   and truecolor paths; the 16-color escape beside it is hand-picked, because
   quantizing five distinct colors down to 3-bit ANSI throws away the very
   distinction that made them five colors. btop reached the same conclusion
   and ships a separate hand-written TTY theme. */
typedef struct {
    unsigned char r, g, b;
    const char *ansi16;
} render_theme_t;

static const render_theme_t THEME[RENDER_SLOT_COUNT] = {
    /* accent */ { 0x5e, 0xc8, 0xf0, "\x1b[96m" },
    /* dim    */ { 0x6a, 0x6a, 0x6a, "\x1b[90m" },
    /* ok     */ { 0x5a, 0xd6, 0x8a, "\x1b[92m" },
    /* warn   */ { 0xe6, 0xc3, 0x4a, "\x1b[93m" },
    /* danger */ { 0xe0, 0x5a, 0x5a, "\x1b[91m" },
};

/* Adapted from btop++ (Apache-2.0), src/btop_theme.cpp:156 truecolor_to_256.
   Integer arithmetic replaces its round(double): for a non-negative integer x,
   (x + 25) / 51 is exactly round(x / 51.0), since no integer x lands on the
   halfway case. Same reasoning for (x + 5) / 11.

   The greyscale branch is worth the extra test because the 24-step ramp reads
   far better than the six grey steps sitting on the cube's diagonal. It also
   inherits btop's quirk that pure black maps to 232 rather than to 16, which
   is one shade off black and harmless for text. */
int render_rgb_to_256(int r, int g, int b) {
    int grey = (r + 5) / 11;
    if (grey == (g + 5) / 11 && grey == (b + 5) / 11) return 232 + grey;
    return 16 + ((r + 25) / 51) * 36 + ((g + 25) / 51) * 6 + ((b + 25) / 51);
}

/* Each depth is one case, so a terminal that can only manage 16 colors takes
   exactly one branch and never sees a sequence it would print as text. */
char *render_style_into(char *buf, size_t n, render_slot_t slot, render_depth_t depth) {
    const render_theme_t *c = &THEME[slot];
    switch (depth) {
        case RENDER_COLOR_TRUE:
            snprintf(buf, n, "\x1b[38;2;%u;%u;%um", c->r, c->g, c->b);
            break;
        case RENDER_COLOR_256:
            snprintf(buf, n, "\x1b[38;5;%dm", render_rgb_to_256(c->r, c->g, c->b));
            break;
        case RENDER_COLOR_16:
            snprintf(buf, n, "%s", c->ansi16);
            break;
        case RENDER_COLOR_NONE:
        default:
            buf[0] = '\0';
            break;
    }
    return buf;
}

/* Worked out once, since the environment cannot change under a running
   process. Everything that emits or clears a color reads it from here, so a
   styled string and the reset that closes it can never disagree about whether
   this terminal gets escapes at all. */
static render_depth_t cached_depth(void) {
    static render_depth_t depth;
    static bool resolved = false;
    if (!resolved) {
        depth = render_depth();
        resolved = true;
    }
    return depth;
}

/* Built once and kept, because the call sites interpolate the result straight
   into a printf without owning it. Tests go through render_style_into and
   never touch this cache. Longest sequence is "\x1b[38;2;255;255;255m" at 19
   bytes. */
const char *render_style(render_slot_t slot) {
    static char cache[RENDER_SLOT_COUNT][24];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < RENDER_SLOT_COUNT; i++)
            render_style_into(cache[i], sizeof(cache[i]), (render_slot_t)i, cached_depth());
        built = true;
    }
    return cache[slot];
}

/* Label rows carry no color of their own, so the name picks one. djb2 over
   the bytes keeps a label the same color on every screen and between runs,
   which is the property that makes the color worth anything: you learn that
   "bug" is the red one. DIM is left out of the rotation because a dimmed
   label reads as disabled rather than as categorized. */
render_slot_t render_slot_for_label(const char *name) {
    static const render_slot_t PALETTE[] = {
        RENDER_ACCENT, RENDER_OK, RENDER_WARN, RENDER_DANGER
    };
    unsigned long h = 5381;
    if (name != NULL) {
        for (const unsigned char *p = (const unsigned char *)name; *p; p++)
            h = h * 33u + *p;
    }
    return PALETTE[h % (sizeof(PALETTE) / sizeof(PALETTE[0]))];
}

/* Parses a positive terminal dimension out of an env var. Returns 0 (meaning
   unusable) on a missing var, garbage, or a non-positive value, so the
   caller can fall through to the next fallback. */
static int env_dimension(const char *name) {
    const char *raw = getenv(name);
    if (raw == NULL || raw[0] == '\0') return 0;
    long val = strtol(raw, NULL, 10);
    return val > 0 ? (int)val : 0;
}

/* ioctl asks the kernel for the terminal's real dimensions, which is the
   only source that's trustworthy when stdout is genuinely a tty. When that
   fails (stdout redirected, or a stub terminal that doesn't answer it), the
   COLUMNS/LINES env vars cover a caller that sets them by hand, and 80x24
   is the last resort so the app always has a mode to render into. */
void render_query_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return;
    }
    int env_cols = env_dimension("COLUMNS");
    int env_rows = env_dimension("LINES");
    if (env_cols > 0 && env_rows > 0) {
        *cols = env_cols;
        *rows = env_rows;
        return;
    }
    *cols = 80;
    *rows = 24;
}

/* Convenience wrapper for callers that only care which mode applies right
   now and don't need the raw cols/rows themselves. */
render_mode_t render_mode(void) {
    int cols, rows;
    render_query_size(&cols, &rows);
    return render_mode_for(cols, rows);
}

/* Clears back to the terminal's default styling. Gated on the depth rather
   than on the color decision alone, so a terminal we decided to send no color
   to never receives a lone reset either. */
const char *render_reset(void) {
    return cached_depth() != RENDER_COLOR_NONE ? "\x1b[0m" : "";
}

/* Asking the codeset is the reliable form of this question. cava reads LANG
   instead and force-sets en_US.utf8 when it is missing, which produces no
   output at all on a machine without that locale installed; its own comment
   cites two bug reports from exactly that. */
bool render_utf8_for(const char *codeset) {
    return codeset != NULL &&
           (strcasecmp(codeset, "UTF-8") == 0 || strcasecmp(codeset, "UTF8") == 0);
}

/* Resolved once. main() calls setlocale before anything draws, so by the time
   this runs nl_langinfo reports the user's real codeset rather than the "C"
   locale every C program starts in. A test binary that never calls setlocale
   gets ASCII, which is the safe answer. */
bool render_utf8(void) {
    static bool resolved = false, answer = false;
    if (!resolved) {
        answer = render_utf8_for(nl_langinfo(CODESET));
        resolved = true;
    }
    return answer;
}

/* Walks the string one multibyte character at a time and adds up what each
   one occupies. A CJK title or an emoji in an issue name counts double here,
   which is what keeps a rule drawn around it the right length. */
int render_display_width(const char *s) {
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    size_t left = strlen(s);
    const char *p = s;
    int cols = 0;
    while (left > 0) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, left, &st);
        /* An incomplete or invalid sequence means this locale cannot read the
           string, so stop guessing per character and return the byte count. */
        if (n == 0 || n == (size_t)-1 || n == (size_t)-2) return (int)strlen(s);
        int w = wcwidth(wc);
        if (w < 0) w = 1;   /* an unprintable one still lands somewhere */
        cols += w;
        p += n;
        left -= n;
    }
    return cols;
}

/* Truncating division is the point, not a shortcut: it guarantees that a
   project with one issue still open cannot show a full bar. The long cast
   keeps a project with a few thousand issues from overflowing the multiply. */
int render_meter_eighths(int done, int total, int cells) {
    if (cells <= 0 || total <= 0 || done <= 0) return 0;
    if (done >= total) return cells * 8;
    return (int)(((long)done * cells * 8) / total);
}

/* One through eight eighths of a cell, filling from the left. Same glyph run
   cava uses for its left-oriented bars, reordered so the index is the number
   of eighths rather than cava's full-block-first layout. These are Unicode
   1.1, unlike the Legacy Computing sextants cava also carries, which show up
   as empty boxes on most machines. */
static const char *const EIGHTHS[9] = {
    "", "▏", "▎", "▍", "▌",
        "▋", "▊", "▉", "█"
};

/* Both branches write exactly `cells` columns, since the ASCII characters are
   one column each and so is every eighth block. The buffer guard leaves room
   for the widest glyph plus its terminator, so a short buffer truncates the
   bar instead of running past the end. */
void render_meter(char *buf, size_t n, int done, int total, int cells, bool utf8) {
    int eighths = render_meter_eighths(done, total, cells);
    int full = eighths / 8, part = eighths % 8;
    size_t at = 0;
    buf[0] = '\0';

    for (int i = 0; i < cells && at + 4 < n; i++) {
        const char *cell;
        if (i < full)               cell = utf8 ? EIGHTHS[8] : "#";
        else if (i == full && part) cell = utf8 ? EIGHTHS[part] : "=";
        else                        cell = utf8 ? "░" : "-";   /* a visible track, not blank */
        at += (size_t)snprintf(buf + at, n - at, "%s", cell);
    }
}

/* Two conditions, one predicate, because getting either one wrong is how a
   decoration ends up inside a redirected log or wrapped across a 40-column
   line. The mode check is not enough on its own: the e2e suite pins COLUMNS
   and LINES at 80x24 while piping stdout, so mode alone reads as FULL there. */
bool render_decorate_for(bool stdout_is_tty, render_mode_t mode) {
    return render_escapes_for(stdout_is_tty) && mode != RENDER_MINIMAL;
}

bool render_decorate(void) {
    return render_decorate_for(isatty(STDOUT_FILENO), render_mode());
}

/* Rounded corners are missing from some older fonts that do have the square
   ones, which is why btop keeps rounding as a preference rather than a
   default. Anyone who hits that has the ASCII table one locale away. */
static const render_box_t BOX_UNICODE = { "╭", "╮", "╰", "╯", "─" };
static const render_box_t BOX_ASCII   = { "+", "+", "+", "+", "-" };

const render_box_t *render_box_for(bool utf8_locale) {
    return utf8_locale ? &BOX_UNICODE : &BOX_ASCII;
}

/* Draws one rule across the full width, with the title inset two columns from
   the left the way btop insets its box titles, so the eye reads the corner
   before the words. A title too long for the line is dropped rather than cut:
   a plain rule says nothing false, a chopped one looks like a bug. */
static void box_rule(bool top, const char *title) {
    const render_box_t *b = render_box_for(render_utf8());
    int cols, rows;
    render_query_size(&cols, &rows);

    int used = 1;
    printf("%s%s", render_style(RENDER_ACCENT), top ? b->tl : b->bl);
    if (title != NULL && title[0] != '\0') {
        int width = render_display_width(title);
        if (width + 6 <= cols) {
            printf("%s%s %s ", b->h, b->h, title);
            used += 4 + width;
        }
    }
    for (int i = used; i < cols - 1; i++) fputs(b->h, stdout);
    printf("%s%s\n", top ? b->tr : b->br, render_reset());
}

void render_title_rule(const char *title) {
    if (!render_decorate()) return;
    box_rule(true, title);
}

/* The hints stay in every mode, unlike the rules and meters around them. They
   are the only place the screen says which keys do anything, so a narrow
   terminal losing them would leave someone with no listed way back out. The
   closing rule lives here because the hints are the last thing every screen
   prints before its prompt, which makes this the one place that knows where
   the content ended. */
void render_help_row(const char *hints) {
    if (render_decorate()) box_rule(false, NULL);
    printf("%s%s%s\n", render_style(RENDER_DIM), hints, render_reset());
}

/* Alt screen first, then hide the cursor, so a terminal that scopes cursor
   visibility to a screen buffer hides the one we are about to draw on.
   Leaving is the mirror: show the cursor and drop any styling while still on
   the alternate buffer, then hand the primary screen back untouched. btop
   shows the cursor after switching back, which blinks it on the restored
   screen for a frame; termbox2 unwinds in the order used here. */
static const char SCREEN_ENTER[] = "\x1b[?1049h\x1b[?25l";
static const char SCREEN_LEAVE[] = "\x1b[?25h\x1b[0m\x1b[?1049l";

static volatile sig_atomic_t screen_taken = 0;

/* Saved by render_splash for the length of the animation. The signal handler
   below needs it because a kill arriving mid-splash would otherwise hand back
   a shell with no echo and no line editing. */
static struct termios splash_termios;
static volatile sig_atomic_t splash_raw = 0;

/* Everything here has to be safe to run from a signal handler, which rules
   out printf and leaves write and tcsetattr. The write result is dropped on
   purpose: if the terminal is already gone there is nothing left to restore
   and no one to tell.

   Known hazard, accepted: this write blocks if the tty output queue is full,
   which is what Ctrl-S (XOFF) does. A signal arriving in that window parks the
   process on the alternate buffer until the queue drains. Making it
   non-blocking would trade a rare stall for a common half-written restore
   sequence, and btop and termbox2 both carry the same exposure. */
static void screen_restore_raw(void) {
    if (screen_taken) {
        ssize_t ignored = write(STDOUT_FILENO, SCREEN_LEAVE, sizeof(SCREEN_LEAVE) - 1);
        (void)ignored;
        screen_taken = 0;
    }
    if (splash_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &splash_termios);
        splash_raw = 0;
    }
}

/* A signal that kills us mid-session would leave the terminal parked on the
   alternate buffer with no cursor, which is the one state a person cannot get
   out of without typing reset(1) blind. Put the screen back, then re-raise so
   the exit status still reports the signal honestly instead of pretending we
   chose to quit. SA_RESETHAND has already put the default disposition back by
   the time this runs, so the re-raise kills us rather than looping. */
static void screen_restore_on_signal(int sig) {
    screen_restore_raw();
    raise(sig);
}

void render_screen_leave(void) {
    if (!screen_taken && !splash_raw) return;
    fflush(stdout);   /* anything still buffered belongs on the screen we are leaving */
    screen_restore_raw();
}

void render_screen_enter(void) {
    if (screen_taken) return;
    if (!render_escapes_for(isatty(STDOUT_FILENO))) return;
    fputs(SCREEN_ENTER, stdout);
    fflush(stdout);
    screen_taken = 1;
    atexit(render_screen_leave);

    /* sigaction rather than signal, whose inherited-disposition and restart
       behavior differs between platforms. SA_RESETHAND arms the handler once,
       which is all we need: it restores the screen and re-raises. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = screen_restore_on_signal;
    sa.sa_flags = SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* The clear is screen control rather than styling, so it answers to the same
   gate as everything else here. It lives in a helper so that "no escape
   sequence outside a gated helper" stays something grep can check. */
static const char *screen_clear(void) {
    return render_escapes_for(isatty(STDOUT_FILENO)) ? "\x1b[H\x1b[2J" : "";
}

/* Mode 2026 tells the terminal to hold the frame until we say we are done.
   btop emits it with no capability check at all, on the reasoning that a
   terminal which does not know a DEC private mode discards it, and that is
   the whole story here too. */
const char *render_sync_begin(void) {
    return render_escapes_for(isatty(STDOUT_FILENO)) ? "\x1b[?2026h" : "";
}

const char *render_sync_end(void) {
    return render_escapes_for(isatty(STDOUT_FILENO)) ? "\x1b[?2026l" : "";
}

/* Frame count and timing for the splash sweep. 8 frames at 120ms is short
   enough that skipping feels instant but still reads as motion if you let
   it play. */
#define SPLASH_FRAMES 8
#define SPLASH_FRAME_MS 120
#define SPLASH_HOLD_MS 600

/* Draws one frame of the sweep: the wordmark as plain text, with an
   accent-colored span overlaid at whatever horizontal offset this frame
   number puts it at. The caller decides pacing; this just paints a frame. */
void splash_render_frame(int frame, int total) {
    int width = wordmark_width();
    /* A quarter of the wordmark width reads as a clear moving band without
       washing the whole thing in color at once. */
    int span = width / 4;
    if (span < 4) span = 4;

    /* Sweep the span from fully off the left edge to fully off the right
       edge over the run, so every column gets colored at some frame. */
    int travel = width + span;
    int start = -span;
    if (total > 1) start = -span + (frame * travel) / (total - 1);

    for (int i = 0; wordmark[i]; i++) {
        const char *row = wordmark[i];
        int len = (int)strlen(row);

        int hi_start = start < 0 ? 0 : start;
        int hi_end = start + span;
        if (hi_end > len) hi_end = len;
        if (hi_start > len) hi_start = len;
        if (hi_end < hi_start) hi_end = hi_start;

        fwrite(row, 1, (size_t)hi_start, stdout);
        if (hi_end > hi_start) {
            fputs(render_style(RENDER_ACCENT), stdout);
            fwrite(row + hi_start, 1, (size_t)(hi_end - hi_start), stdout);
            fputs(render_reset(), stdout);
        }
        fputs(row + hi_end, stdout);
        fputc('\n', stdout);
    }
}

/* Waits up to ms milliseconds for a byte on stdin. A pending byte is read
   and discarded here rather than left for the menu loop, since it exists
   only to mean "skip" and would otherwise show up as a stray character on
   the next real prompt. Returns true if a key arrived before the timeout. */
static bool wait_for_key(int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) > 0 && FD_ISSET(STDIN_FILENO, &set)) {
        char discard;
        if (read(STDIN_FILENO, &discard, 1) < 0) { /* nothing to do, still treat as skipped */ }
        return true;
    }
    /* A -1 return (EINTR from a signal) lands here with the plain timeout, and
       that is what we want. The caller just moves on to the next frame, so the
       only cost is one frame holding for less than its full 120ms. The two
       handlers this program installs both re-raise and die, so in practice
       nothing ever comes back through here. */
    return false;
}

/* Plays the animated wordmark once, in the one mode with room for it. The
   terminal only ever gets put into raw mode below, after saved has captured
   the original state, and every return before that point leaves stdin
   untouched, so the restore at the bottom of this function always has a
   matching setup to undo and is never skipped once raw mode is entered. */
void render_splash(void) {
    // a script piping into or out of the app has no one to watch the splash
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;

    render_mode_t mode = render_mode();
    if (mode == RENDER_MINIMAL) return;   // no room for even the static mark
    if (mode == RENDER_COMPACT) {
        printf("%s\n", logo_compact);
        return;
    }

    struct termios saved;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) {
        /* Without the original state there is nothing safe to restore
           later, so skip the raw mode and animation entirely rather than
           risk leaving the terminal in whatever mode we set it to. */
        printf("%s\n", logo_compact);
        return;
    }
    /* Canonical mode buffers input by line and echoes it, neither of which
       works for polling a single skip keypress mid-animation. VMIN/VTIME 0
       makes reads non-blocking so wait_for_key's select can own the timing.

       ISIG goes too, so Ctrl-C during the animation arrives as an ordinary
       0x03 byte instead of a SIGINT. Read as a byte it counts as the skip
       keypress, which is what someone mashing Ctrl-C is asking for, and the
       restore below still runs. The same reasoning covers Ctrl-Z and
       Ctrl-backslash: a suspend taken mid-raw-mode leaves the terminal as
       broken as a kill would, so SUSP and QUIT get the same plain-byte
       treatment. What ISIG cannot cover is a SIGTERM from elsewhere, which is
       why the saved settings are published to the signal handler below before
       raw mode goes on and withdrawn the moment it comes off. */
    splash_termios = saved;
    /* The flag is what tells the handler the struct beside it is worth
       reading, so the struct has to be written first. Without the fence that
       ordering is whatever the optimizer felt like, and a handler firing in
       between would restore an uninitialized termios. */
    atomic_signal_fence(memory_order_release);
    splash_raw = 1;
    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    bool skipped = false;
    for (int i = 0; i < SPLASH_FRAMES && !skipped; i++) {
        /* Home before clear, so a terminal that ignores the sync markers
           still never shows the cursor parked at the bottom of the wipe. */
        printf("%s%s", render_sync_begin(), screen_clear());
        splash_render_frame(i, SPLASH_FRAMES);
        printf("%spress any key to skip%s\n", render_style(RENDER_DIM), render_reset());
        printf("%s", render_sync_end());
        fflush(stdout);
        skipped = wait_for_key(SPLASH_FRAME_MS);
    }
    if (!skipped) wait_for_key(SPLASH_HOLD_MS);

    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    splash_raw = 0;
    /* Keys mashed during the splash would otherwise sit in the tty buffer and
       land in the first menu prompt. */
    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("%s", screen_clear());
    fflush(stdout);
}

#include "render.h"
#include "assets.h"
#include <langinfo.h>
#include <limits.h>
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

static const render_theme_t THEME_DARK[RENDER_SLOT_COUNT] = {
    /* accent */ { 0x5e, 0xc8, 0xf0, "\x1b[96m" },
    /* dim    */ { 0x6a, 0x6a, 0x6a, "\x1b[90m" },
    /* ok     */ { 0x5a, 0xd6, 0x8a, "\x1b[92m" },
    /* warn   */ { 0xe6, 0xc3, 0x4a, "\x1b[93m" },
    /* danger */ { 0xe0, 0x5a, 0x5a, "\x1b[91m" },
};

/* The dark table's pastel cyan and light greens/yellows wash out to near
   nothing on a white background. This is GitHub's own light theme palette,
   not one picked by eye, since that is a contrast choice already made and
   tested by someone else. */
static const render_theme_t THEME_LIGHT[RENDER_SLOT_COUNT] = {
    /* accent */ { 0x09, 0x69, 0xda, "\x1b[34m" },
    /* dim    */ { 0x57, 0x60, 0x6a, "\x1b[90m" },
    /* ok     */ { 0x1a, 0x7f, 0x37, "\x1b[32m" },
    /* warn   */ { 0x9a, 0x67, 0x00, "\x1b[33m" },
    /* danger */ { 0xcf, 0x22, 0x2e, "\x1b[31m" },
};

static const render_theme_t *theme_for(render_background_t bg) {
    return bg == RENDER_BG_LIGHT ? THEME_LIGHT : THEME_DARK;
}

/* COLORFGBG is "fg;bg" or "fg;unused;bg" depending on the terminal, so the
   background is always the last field rather than a fixed index. 7 and the
   9-15 range are the light backgrounds a terminal reports (white and the
   bright colors); everything else, including anything we can't parse, reads
   as dark, which is what color has always meant in this app. */
render_background_t render_background_for(const char *colorfgbg_env) {
    if (colorfgbg_env == NULL || colorfgbg_env[0] == '\0') return RENDER_BG_DARK;

    const char *last = strrchr(colorfgbg_env, ';');
    const char *field = last != NULL ? last + 1 : colorfgbg_env;

    char *end;
    long v = strtol(field, &end, 10);
    if (end == field || *end != '\0') return RENDER_BG_DARK;   // not a plain integer

    if (v == 7 || (v >= 9 && v <= 15)) return RENDER_BG_LIGHT;
    return RENDER_BG_DARK;
}

render_background_t render_background(void) {
    return render_background_for(getenv("COLORFGBG"));
}

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

/* Truncating integer lerp, same as the dossier's lerp8: a one-unit rounding
   error is invisible in a color ramp and not worth the branch correct
   rounding of a negative delta would need. col is clamped first so the
   division below never sees a col outside [0, width-1] and a caller walking
   one column past either edge still gets the matching endpoint color instead
   of undefined behavior. */
void render_gradient_at(int col, int width, int r0, int g0, int b0,
                        int r1, int g1, int b1, int *r, int *g, int *b) {
    if (width < 2) width = 2;
    if (col < 0) col = 0;
    if (col > width - 1) col = width - 1;
    int steps = width - 1;
    *r = r0 + (r1 - r0) * col / steps;
    *g = g0 + (g1 - g0) * col / steps;
    *b = b0 + (b1 - b0) * col / steps;
}

/* Each depth is one case, so a terminal that can only manage 16 colors takes
   exactly one branch and never sees a sequence it would print as text. */
char *render_style_into(char *buf, size_t n, render_slot_t slot, render_depth_t depth,
                        render_background_t bg) {
    const render_theme_t *c = &theme_for(bg)[slot];
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

/* Same reasoning as cached_depth: COLORFGBG cannot change under a running
   process, so every call site should see the same table for the whole run. */
static render_background_t cached_background(void) {
    static render_background_t bg;
    static bool resolved = false;
    if (!resolved) {
        bg = render_background();
        resolved = true;
    }
    return bg;
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
            render_style_into(cache[i], sizeof(cache[i]), (render_slot_t)i, cached_depth(),
                              cached_background());
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

/* Copies s into out with every CSI sequence (ESC '[' ... final byte) removed,
   so a caller measuring or truncating text already wrapped in render_style()
   does not count the color codes as columns. An unterminated sequence at the
   end of s is dropped rather than copied, since a truncated escape code is
   never something a caller wants to keep. */
static size_t strip_escapes(const char *s, char *out, size_t outsz) {
    size_t oi = 0;
    for (const char *p = s; *p != '\0' && oi + 1 < outsz; ) {
        if ((unsigned char)p[0] == 0x1b && p[1] == '[') {
            const char *q = p + 2;
            while (*q != '\0' && !((unsigned char)*q >= 0x40 && (unsigned char)*q <= 0x7e)) q++;
            p = (*q != '\0') ? q + 1 : q;
            continue;
        }
        out[oi++] = *p++;
    }
    out[oi] = '\0';
    return oi;
}

int render_visible_width(const char *s) {
    char plain[1024];
    strip_escapes(s, plain, sizeof(plain));
    return render_display_width(plain);
}

/* Adapted in spirit from the trap the dossier flags in btop's uresize
   (btop_tools.cpp:281): that truncates by popping one character at a time and
   recomputing the whole string's width on every pop, which is O(n^2). This
   instead walks s once, accumulating wcwidth as it goes, and stops the moment
   the budget for the ellipsis would be exceeded. A character is only ever
   committed whole, so the cut point can never land inside a wide one. */
char *render_truncate(const char *s, int cols, bool utf8, char *out, size_t outsz) {
    if (cols < 0) cols = 0;
    if (render_display_width(s) <= cols) {
        snprintf(out, outsz, "%s", s);
        return out;
    }

    const char *ellipsis = utf8 ? "\xe2\x80\xa6" : "...";
    int ellipsis_w = utf8 ? 1 : 3;
    bool fits_ellipsis = ellipsis_w <= cols;
    int budget = fits_ellipsis ? cols - ellipsis_w : cols;

    mbstate_t st;
    memset(&st, 0, sizeof(st));
    size_t left = strlen(s);
    const char *p = s;
    int used_w = 0;
    size_t used_b = 0;
    while (left > 0) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, left, &st);
        if (n == 0 || n == (size_t)-1 || n == (size_t)-2) break;   // undecodable tail, stop here
        int w = wcwidth(wc);
        if (w < 0) w = 1;
        if (used_w + w > budget) break;
        used_w += w;
        used_b += n;
        p += n;
        left -= n;
    }

    /* No ellipsis at all when there was no room to fit even one: a hard cut
       still respects the requested width, which a truncated ellipsis would not. */
    size_t elen = fits_ellipsis ? strlen(ellipsis) : 0;
    if (used_b + elen >= outsz)
        used_b = outsz > elen + 1 ? outsz - elen - 1 : 0;
    memcpy(out, s, used_b);
    memcpy(out + used_b, ellipsis, elen);
    out[used_b + elen] = '\0';
    return out;
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
static const render_box_t BOX_UNICODE = { "╭", "╮", "╰", "╯", "─", "│" };
static const render_box_t BOX_ASCII   = { "+", "+", "+", "+", "-", "|" };

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

/* Left border, text, right border. content is cols minus the two border
   columns and the one padding space on each side of them. styled_text may
   carry render_style() escapes; strip_escapes measures past them so a
   colored caret or label name pads out to the same width a plain one would.
   A line too wide for the box falls back to its plain text, truncated with
   an ellipsis, rather than pushing the right border off the edge of the
   screen: that only costs color on a line long enough to need cutting, and
   no interactive row is that long by design. */
void render_box_line(const char *styled_text) {
    if (!render_decorate()) return;
    const render_box_t *b = render_box_for(render_utf8());
    int cols, rows;
    render_query_size(&cols, &rows);
    int content = cols - 4;
    if (content < 0) content = 0;

    char plain[1024];
    strip_escapes(styled_text, plain, sizeof(plain));
    int w = render_display_width(plain);

    printf("%s%s%s ", render_style(RENDER_ACCENT), b->v, render_reset());
    const char *body = styled_text;
    char cut[1024];
    if (w > content) {
        render_truncate(plain, content, render_utf8(), cut, sizeof(cut));
        body = cut;
        w = render_display_width(cut);
    }
    printf("%s", body);
    for (int i = w; i < content; i++) putchar(' ');
    printf("%s %s%s%s\n", render_reset(), render_style(RENDER_ACCENT), b->v, render_reset());
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

/* Saved by render_raw_enter for as long as raw mode is on. The signal handler
   below needs it because a kill arriving while the splash animates, or while
   the menu waits on an arrow key, would otherwise hand back a shell with no
   echo and no line editing. */
static struct termios raw_termios;
static volatile sig_atomic_t raw_active = 0;

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
    if (raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
        raw_active = 0;
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

/* Both ways of taking the terminal over need the same unwinding, so whichever
   one happens first arms it for both. The guard is what keeps a menu that
   enters and leaves raw mode on every keystroke from stacking up a fresh
   atexit entry each time. */
static void install_restore(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;
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

void render_screen_leave(void) {
    if (!screen_taken && !raw_active) return;
    fflush(stdout);   /* anything still buffered belongs on the screen we are leaving */
    screen_restore_raw();
}

void render_screen_enter(void) {
    if (screen_taken) return;
    if (!render_escapes_for(isatty(STDOUT_FILENO))) return;
    fputs(SCREEN_ENTER, stdout);
    fflush(stdout);
    screen_taken = 1;
    install_restore();
}

/* ICANON and ECHO go so a keypress arrives on its own instead of waiting for
   Enter and printing itself. ISIG goes with them, which turns Ctrl-C into an
   ordinary 0x03 byte: the reader treats it as "back", and that is safer than
   a SIGINT landing while the terminal is in this state. VMIN 1 makes a read
   wait for a byte, which is what a caller that has already selected on stdin
   wants. What ISIG cannot cover is a signal from elsewhere, which is why the
   previous settings are published to the handler before the switch. */
bool render_raw_enter(void) {
    if (raw_active) return true;
    struct termios saved;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return false;

    raw_termios = saved;
    /* The flag is what tells the handler the struct beside it is worth
       reading, so the struct has to be written first. Without the fence that
       ordering is whatever the optimizer felt like, and a handler firing in
       between would restore an uninitialized termios. */
    atomic_signal_fence(memory_order_release);
    raw_active = 1;

    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    install_restore();
    return true;
}

void render_raw_leave(void) {
    if (!raw_active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
    raw_active = 0;
}

/* The clear is screen control rather than styling, so it answers to the same
   gate as everything else here. It lives in a helper so that "no escape
   sequence outside a gated helper" stays something grep can check. */
const char *render_clear(void) {
    return render_escapes_for(isatty(STDOUT_FILENO)) ? "\x1b[H\x1b[2J" : "";
}

/* The alternate screen hides the cursor for the whole session, which is right
   while the menu is being navigated and wrong the moment someone is asked to
   type a name. */
const char *render_cursor(bool show) {
    if (!render_escapes_for(isatty(STDOUT_FILENO))) return "";
    return show ? "\x1b[?25h" : "\x1b[?25l";
}

/* Reverse video, gated the same way as render_clear and render_cursor: it is
   a screen effect that answers to whether a terminal is watching, not to
   whether the user asked for no color. */
const char *render_flash(void) {
    return render_escapes_for(isatty(STDOUT_FILENO)) ? "\x1b[7m" : "";
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

/* The gradient's two stops, per background. Violet against the dark table's
   cyan accent and against the light table's blue both read as a deliberate
   two-color mark rather than a random hue shift; 0x8250df is GitHub's own
   purple, so the light pairing is two colors GitHub already ships next to
   each other. Only consulted at RENDER_COLOR_256 and above: RENDER_COLOR_16
   has no room for a second hue without muddying the one it already has. */
typedef struct { unsigned char r0, g0, b0, r1, g1, b1; } render_gradient_t;
static const render_gradient_t GRADIENT_DARK  = { 0x5e, 0xc8, 0xf0, 0xb0, 0x7c, 0xe8 };
static const render_gradient_t GRADIENT_LIGHT = { 0x09, 0x69, 0xda, 0x82, 0x50, 0xdf };

/* Draws one row of the gradient wordmark, brightening the band columns
   toward white so the sweep still reads as a moving highlight over a
   wordmark that is now colored everywhere rather than just under the band.
   Escapes are only re-emitted when the color actually changes: at
   RENDER_COLOR_256 several adjacent columns often quantize to the same
   index, and skipping the repeat keeps the frame from bloating into one
   escape per character for no visible difference. */
static void gradient_row(const char *row, int width, int band_lo, int band_hi,
                         const render_gradient_t *grad, render_depth_t depth) {
    int len = (int)strlen(row);
    int last_key = INT_MIN;   // never matches a real 256-index or packed RGB

    for (int i = 0; i < len; i++) {
        int r, g, b;
        render_gradient_at(i, width, grad->r0, grad->g0, grad->b0,
                           grad->r1, grad->g1, grad->b1, &r, &g, &b);
        if (i >= band_lo && i < band_hi) {
            r += (255 - r) / 2;
            g += (255 - g) / 2;
            b += (255 - b) / 2;
        }
        int key = depth == RENDER_COLOR_TRUE ? (r << 16) | (g << 8) | b
                                              : render_rgb_to_256(r, g, b);
        if (key != last_key) {
            if (depth == RENDER_COLOR_TRUE) printf("\x1b[38;2;%d;%d;%dm", r, g, b);
            else                            printf("\x1b[38;5;%dm", key);
            last_key = key;
        }
        putchar(row[i]);
    }
    fputs(render_reset(), stdout);
}

/* Draws one frame of the sweep. At RENDER_COLOR_256 and above the whole
   wordmark carries the gradient and the moving band brightens the columns
   under it; below that (16 colors, or none) there is no second hue to spend,
   so the band stays the flat accent-colored span this always drew, over
   plain text everywhere else. The caller decides pacing; this just paints a
   frame. */
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

    render_depth_t depth = cached_depth();
    const render_gradient_t *grad =
        cached_background() == RENDER_BG_LIGHT ? &GRADIENT_LIGHT : &GRADIENT_DARK;

    for (int i = 0; wordmark[i]; i++) {
        const char *row = wordmark[i];
        int len = (int)strlen(row);

        int hi_start = start < 0 ? 0 : start;
        int hi_end = start + span;
        if (hi_end > len) hi_end = len;
        if (hi_start > len) hi_start = len;
        if (hi_end < hi_start) hi_end = hi_start;

        if (depth < RENDER_COLOR_256) {
            fwrite(row, 1, (size_t)hi_start, stdout);
            if (hi_end > hi_start) {
                fputs(render_style(RENDER_ACCENT), stdout);
                fwrite(row + hi_start, 1, (size_t)(hi_end - hi_start), stdout);
                fputs(render_reset(), stdout);
            }
            fputs(row + hi_end, stdout);
        } else {
            gradient_row(row, width, hi_start, hi_end, grad, depth);
        }
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

/* Plays the animated wordmark once, in the one mode with room for it. Raw
   mode is entered after every early return above it, so the leave at the
   bottom always has a matching enter to undo and is never skipped once the
   animation has started. Ctrl-C during it counts as the skip keypress, since
   it arrives as a byte, which is what someone mashing it is asking for. */
void render_splash(void) {
    // a script piping into or out of the app has no one to watch the splash
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;

    render_mode_t mode = render_mode();
    if (mode == RENDER_MINIMAL) return;   // no room for even the static mark
    if (mode == RENDER_COMPACT) {
        printf("%s\n", logo_compact);
        return;
    }
    /* Without the original settings there is nothing safe to restore later,
       so a terminal we cannot read gets the static mark instead of an
       animation we would have to leave it in raw mode for. */
    if (!render_raw_enter()) {
        printf("%s\n", logo_compact);
        return;
    }

    bool skipped = false;
    for (int i = 0; i < SPLASH_FRAMES && !skipped; i++) {
        /* Home before clear, so a terminal that ignores the sync markers
           still never shows the cursor parked at the bottom of the wipe. */
        printf("%s%s", render_sync_begin(), render_clear());
        splash_render_frame(i, SPLASH_FRAMES);
        printf("%spress any key to skip%s\n", render_style(RENDER_DIM), render_reset());
        printf("%s", render_sync_end());
        fflush(stdout);
        skipped = wait_for_key(SPLASH_FRAME_MS);
    }
    if (!skipped) wait_for_key(SPLASH_HOLD_MS);

    render_raw_leave();
    /* Keys mashed during the splash would otherwise sit in the tty buffer and
       land in the first menu prompt. */
    tcflush(STDIN_FILENO, TCIFLUSH);
    printf("%s", render_clear());
    fflush(stdout);
}

/* Braille dots read as motion at a glance on any terminal font with the
   block; the ASCII set is the same spin every terminal can show regardless
   of locale. */
static const char *const SPINNER_BRAILLE[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8", "\xe2\xa0\xbc",
    "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87", "\xe2\xa0\x8f",
};
static const char *const SPINNER_ASCII[] = { "|", "/", "-", "\\" };

const char *render_spinner_frame(int frame, bool utf8) {
    if (frame < 0) frame = 0;
    if (utf8) return SPINNER_BRAILLE[frame % (int)(sizeof(SPINNER_BRAILLE) / sizeof(SPINNER_BRAILLE[0]))];
    return SPINNER_ASCII[frame % (int)(sizeof(SPINNER_ASCII) / sizeof(SPINNER_ASCII[0]))];
}

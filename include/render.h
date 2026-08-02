#ifndef RENDER_H
#define RENDER_H
#include <stdbool.h>
#include <stddef.h>

/* Three tiers the UI degrades through as the terminal shrinks. FULL gets art
   and color, COMPACT drops the art, MINIMAL is text only. */
typedef enum { RENDER_FULL, RENDER_COMPACT, RENDER_MINIMAL } render_mode_t;

/* Pure classification so the breakpoints are unit tested without a real
   terminal. FULL needs both dimensions to clear 80x24; COMPACT only needs
   width, since a long scroll is more tolerable than a clipped line. */
render_mode_t render_mode_for(int cols, int rows);

/* Whether writing terminal control sequences is safe at all, which comes down
   to having a real terminal reading stdout. Kept separate from the color
   decision below because NO_COLOR asks for plain text, not for a hidden
   cursor or a scrolling screen. */
bool render_escapes_for(bool stdout_is_tty);

/* Pure color decision so NO_COLOR and tty handling are unit tested without
   touching isatty/getenv. A set no_color_env disables color even on a tty;
   NULL or empty means unset. */
bool render_color_for(bool stdout_is_tty, const char *no_color_env);

/* How much color the terminal on the other end of stdout can show. The order
   matters: every level covers the one before it, so a caller that needs at
   least 256 colors can ask with a single >= comparison. */
typedef enum {
    RENDER_COLOR_NONE = 0,
    RENDER_COLOR_16,
    RENDER_COLOR_256,
    RENDER_COLOR_TRUE
} render_depth_t;

/* Reads the depth out of COLORTERM and TERM. Pure, so the whole heuristic is
   unit tested with no terminal anywhere; any argument may be NULL, which
   reads as an unset variable. */
render_depth_t render_depth_for(bool stdout_is_tty, const char *no_color_env,
                                const char *colorterm_env, const char *term_env);

/* render_depth_for wired to the live isatty and getenv. */
render_depth_t render_depth(void);

/* What a piece of text means, rather than what color it is. Call sites pick a
   slot and the theme decides how that looks at whatever depth the terminal
   turned out to have. */
typedef enum {
    RENDER_ACCENT = 0,
    RENDER_DIM,
    RENDER_OK,
    RENDER_WARN,
    RENDER_DANGER,
    RENDER_SLOT_COUNT
} render_slot_t;

/* Escape sequence for one slot at the live depth, or "" when there is no
   color. Same promise the old render_accent made: safe to drop into any
   printf with no guard around it. */
const char *render_style(render_slot_t slot);

/* The builder behind render_style, exposed so a test can ask for any slot at
   any depth without touching the environment. Writes a NUL-terminated escape
   into buf and hands buf back. */
char *render_style_into(char *buf, size_t n, render_slot_t slot, render_depth_t depth);

/* Nearest xterm-256 index for an RGB triple. Public for the unit test; the
   only caller in the app is the 256-color branch of render_style_into. */
int render_rgb_to_256(int r, int g, int b);

/* A color for a label name, picked by hashing the name so the same label
   keeps the same color across screens and across runs. Labels are local rows
   with no color of their own, so this stands in for the one GitHub would
   have sent us. */
render_slot_t render_slot_for_label(const char *name);

/* Whether a locale codeset names UTF-8, which is what decides between the
   Unicode glyphs and the ASCII ones. Hand it nl_langinfo(CODESET); NULL
   reads as "cannot tell", which falls back to ASCII. */
bool render_utf8_for(const char *codeset);

/* render_utf8_for asked about the live locale, answered once and kept. */
bool render_utf8(void);

/* Columns a string takes up on screen, which stops matching strlen the
   moment a title carries anything outside ASCII. Falls back to the byte
   count when the locale cannot decode the bytes, which is the same answer
   for plain ASCII and an over-estimate that only costs a short rule. */
int render_display_width(const char *s);

/* How many eighths of a cell should be filled to show done out of total
   across `cells` columns, from 0 to cells*8. Pure integer arithmetic, so the
   two ends are a unit test rather than a screenshot: nothing done leaves the
   bar empty, everything done fills it, and a single outstanding item can
   never round up into a full bar. */
int render_meter_eighths(int done, int total, int cells);

/* Writes a bar exactly `cells` columns wide into buf. Eighth-block glyphs
   when the locale can decode them, a #/=/- bar when it cannot. */
void render_meter(char *buf, size_t n, int done, int total, int cells, bool utf8);

/* Whether decoration is worth drawing at all: a real terminal is watching,
   and it has room to spare. Piped output stays plain on purpose, so a
   redirect into a file or into the e2e harness reads as text rather than as
   a screenshot of a screen nobody saw. Every box, meter and rule goes
   through this one predicate. */
bool render_decorate(void);

/* The decision behind render_decorate, taking both inputs as arguments so the
   tty and mode halves are unit tested without either one being real. */
bool render_decorate_for(bool stdout_is_tty, render_mode_t mode);

/* The pieces a rule is drawn from, held as UTF-8 strings rather than chars
   because a box-drawing glyph is three bytes. There is no vertical here: the
   screens scroll and print lines of whatever length they like, so the box
   brackets the content top and bottom instead of enclosing it. */
typedef struct {
    const char *tl, *tr, *bl, *br, *h;
} render_box_t;

/* Rounded Unicode when the terminal can decode UTF-8, ASCII when it cannot.
   Pure, so both tables are tested without touching the locale. */
const render_box_t *render_box_for(bool utf8_locale);

/* Opens a screen with a rule carrying the title inset from the left corner.
   Draws nothing at all unless render_decorate() agrees, which is what keeps
   piped output free of it. Sized to the terminal's current width, so a
   window resized between screens is picked up on the next one. */
void render_title_rule(const char *title);

/* One dim line of key hints, printed just above the prompt, closed off by the
   matching bottom rule when the screen is being decorated. The text goes out
   exactly as it arrives, so screens keep control of their own wording and the
   e2e suite keeps matching plain substrings. */
void render_help_row(const char *hints);

/* Fills cols/rows from ioctl(TIOCGWINSZ) on stdout, falling back to the
   COLUMNS/LINES environment variables, then to 80x24 if nothing usable is
   found. */
void render_query_size(int *cols, int *rows);

/* render_query_size + render_mode_for, for callers that just want the mode. */
render_mode_t render_mode(void);

/* Clears whatever render_style set, and returns "" wherever render_style
   would, so the two always agree about whether this terminal gets escapes. */
const char *render_reset(void);

/* Takes the screen over: alternate buffer, cursor hidden, restored on every
   exit path this program can survive. A no-op when stdout is not a terminal,
   and safe to call twice, so no caller has to track whether it already ran.
   Installs the SIGINT and SIGTERM handlers that put the screen back, since
   atexit does not run when a signal kills the process. */
void render_screen_enter(void);

/* Hands the primary screen back with the cursor showing. Registered with
   atexit by render_screen_enter, so most callers never need it. */
void render_screen_leave(void);

/* Brackets one repaint so the terminal shows a finished frame instead of
   painting it piece by piece. Both return "" off a terminal. Terminals that
   have never heard of mode 2026 drop the sequence, so there is no capability
   to check and nothing to ask them. */
const char *render_sync_begin(void);
const char *render_sync_end(void);

/* Prints one frame of the intro sweep: the wordmark from assets.h with a
   moving accent-colored column span. frame is the 0-based current index,
   total is the frame count, so the caller (render_splash) owns the timing
   and this just draws a single frame each call. */
void splash_render_frame(int frame, int total);

/* Plays the animated intro once in FULL mode, prints the static compact logo
   in COMPACT, and does nothing in MINIMAL. Skippable on any keypress,
   Ctrl-C included: it skips the animation rather than quitting, since killing
   the process mid-splash is what would strand the terminal in raw mode.
   A no-op unless both stdin and stdout are ttys, so piped runs never see it
   or block on it. Restores stdin's termios before returning on every path. */
void render_splash(void);

#endif

#ifndef RENDER_H
#define RENDER_H
#include <stdbool.h>

/* Three tiers the UI degrades through as the terminal shrinks. FULL gets art
   and color, COMPACT drops the art, MINIMAL is text only. */
typedef enum { RENDER_FULL, RENDER_COMPACT, RENDER_MINIMAL } render_mode_t;

/* Pure classification so the breakpoints are unit tested without a real
   terminal. FULL needs both dimensions to clear 80x24; COMPACT only needs
   width, since a long scroll is more tolerable than a clipped line. */
render_mode_t render_mode_for(int cols, int rows);

/* Pure color decision so NO_COLOR and tty handling are unit tested without
   touching isatty/getenv. A set no_color_env disables color even on a tty;
   NULL or empty means unset. */
bool render_color_for(bool stdout_is_tty, const char *no_color_env);

/* Fills cols/rows from ioctl(TIOCGWINSZ) on stdout, falling back to the
   COLUMNS/LINES environment variables, then to 80x24 if nothing usable is
   found. */
void render_query_size(int *cols, int *rows);

/* render_query_size + render_mode_for, for callers that just want the mode. */
render_mode_t render_mode(void);

/* render_color_for wired to the real isatty/getenv, for callers outside a
   test that just want the live decision. */
bool render_color(void);

/* Emits ESC[8;24;80t when stdout is a tty, asking the terminal to resize to
   80x24. Most terminals ignore this, so callers must re-query the size
   afterward rather than assume it worked. A no-op when stdout is not a tty. */
void render_request_size(void);

/* ANSI helpers for the menu to print unconditionally. Each returns its escape
   code when render_color() is true and an empty string otherwise, so a
   caller never needs an if around them. */
const char *render_accent(void);
const char *render_dim(void);
const char *render_reset(void);

/* Prints one frame of the intro sweep: the wordmark from assets.h with a
   moving accent-colored column span. frame is the 0-based current index,
   total is the frame count, so the caller (render_splash) owns the timing
   and this just draws a single frame each call. */
void splash_render_frame(int frame, int total);

/* Plays the animated intro once in FULL mode, prints the static compact logo
   in COMPACT, and does nothing in MINIMAL. Skippable on any keypress.
   A no-op unless both stdin and stdout are ttys, so piped runs never see it
   or block on it. Restores stdin's termios before returning on every path. */
void render_splash(void);

#endif

#include "render.h"
#include "assets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>

render_mode_t render_mode_for(int cols, int rows) {
    if (cols >= 80 && rows >= 24) return RENDER_FULL;
    if (cols >= 40) return RENDER_COMPACT;
    return RENDER_MINIMAL;
}

bool render_color_for(bool stdout_is_tty, const char *no_color_env) {
    if (!stdout_is_tty) return false;
    return no_color_env == NULL || no_color_env[0] == '\0';
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

render_mode_t render_mode(void) {
    int cols, rows;
    render_query_size(&cols, &rows);
    return render_mode_for(cols, rows);
}

bool render_color(void) {
    return render_color_for(isatty(STDOUT_FILENO), getenv("NO_COLOR"));
}

void render_request_size(void) {
    /* Fire-and-forget: most terminals ignore a resize request, and we
       re-query afterward regardless, so there is nothing to check here. */
    if (isatty(STDOUT_FILENO)) {
        write(STDOUT_FILENO, "\x1b[8;24;80t", 10);
    }
}

const char *render_accent(void) {
    return render_color() ? "\x1b[96m" : "";
}

const char *render_dim(void) {
    return render_color() ? "\x1b[2m" : "";
}

const char *render_reset(void) {
    return render_color() ? "\x1b[0m" : "";
}

/* Frame count and timing for the splash sweep. 8 frames at 120ms is short
   enough that skipping feels instant but still reads as motion if you let
   it play. */
#define SPLASH_FRAMES 8
#define SPLASH_FRAME_MS 120
#define SPLASH_HOLD_MS 600

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
            fputs(render_accent(), stdout);
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
    return false;
}

void render_splash(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;

    render_mode_t mode = render_mode();
    if (mode == RENDER_MINIMAL) return;
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
    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    bool skipped = false;
    for (int i = 0; i < SPLASH_FRAMES && !skipped; i++) {
        printf("\x1b[2J\x1b[H");
        splash_render_frame(i, SPLASH_FRAMES);
        printf("%spress any key to skip%s\n", render_dim(), render_reset());
        fflush(stdout);
        skipped = wait_for_key(SPLASH_FRAME_MS);
    }
    if (!skipped) wait_for_key(SPLASH_HOLD_MS);

    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
}

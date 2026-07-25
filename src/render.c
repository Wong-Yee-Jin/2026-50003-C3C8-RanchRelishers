#include "render.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

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

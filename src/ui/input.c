#include "ui/input.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

/* How long to wait for the rest of an escape sequence before calling it a
   lone Esc press. Every terminal sends the bytes of an arrow key in one go,
   so this only has to outlast the write, not a human. termbox2 waits 25ms and
   btop polls at 10ms; 75 leaves room for a slow ssh hop without being long
   enough to feel like lag on the one key that pays it. */
#define ESC_TIMEOUT_MS 75

/* An arrow key is three bytes and a mouse report is under twenty, so this
   holds any sequence a terminal sends plus whatever was typed behind it. */
#define KEY_BUF 32

static key_event_t event(key_type_t type, char ch) {
    key_event_t ev = { type, ch };
    return ev;
}

/* Everything that starts with ESC. Two shapes matter: CSI (ESC [ ...) which
   is what a terminal sends by default, and SS3 (ESC O ...) which is the same
   arrows in application cursor mode. Both are accepted because which one
   arrives depends on the terminal's mode rather than on anything we set. */
static size_t decode_escape(const char *buf, size_t len, key_event_t *out) {
    /* A lone Esc looks exactly like the first byte of an arrow key, so it
       stays undecided here and input_read_key resolves it with a timeout. */
    if (len < 2) return 0;

    char kind = buf[1];
    if (kind != '[' && kind != 'O') {
        /* Alt-<key>, which we have nothing to do with. Consumed anyway, or
           the character behind the ESC would arrive as a menu keystroke the
           user never meant to send. */
        out->type = KEY_IGNORED;
        return 2;
    }
    if (len < 3) return 0;

    if (buf[2] == 'A') { out->type = KEY_UP;   return 3; }
    if (buf[2] == 'B') { out->type = KEY_DOWN; return 3; }

    /* SS3 carries exactly one byte after the O, so whatever it was, it ends
       here. */
    if (kind == 'O') {
        out->type = KEY_IGNORED;
        return 3;
    }

    /* A CSI sequence runs until a byte in the 0x40..0x7e final range. Reading
       to that byte is what keeps a function key or a mouse report from
       spilling its parameter digits into the menu one at a time. */
    for (size_t i = 2; i < len; i++) {
        unsigned char b = (unsigned char)buf[i];
        if (b >= 0x40 && b <= 0x7e) {
            out->type = KEY_IGNORED;
            return i + 1;
        }
    }
    return 0;
}

size_t input_decode(const char *buf, size_t len, key_event_t *out) {
    *out = event(KEY_NONE, 0);
    if (len == 0) return 0;

    unsigned char c = (unsigned char)buf[0];
    if (c == 0x1b) return decode_escape(buf, len, out);

    /* A terminal in raw mode sends \r for Enter, but a pty driven by a script
       may well send \n, and both mean the same thing here. */
    if (c == '\r' || c == '\n') { out->type = KEY_ENTER;     return 1; }
    if (c == 0x03)              { out->type = KEY_INTERRUPT; return 1; }
    if (c == 0x04)              { out->type = KEY_EOF;       return 1; }

    if (c >= 0x20 && c < 0x7f) {
        out->type = KEY_CHAR;
        out->ch = (char)c;
        return 1;
    }

    /* Tab, backspace, the rest of the control range, and any byte above ASCII.
       Nothing on these screens is typed in raw mode, so there is no text for a
       multibyte character to be part of. */
    out->type = KEY_IGNORED;
    return 1;
}

/* Waits for stdin to have something to read. A negative ms blocks until it
   does; anything else gives up after that long and returns false. */
static bool wait_readable(int ms) {
    for (;;) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;

        int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, ms < 0 ? NULL : &tv);
        if (ready > 0) return true;
        if (ready == 0) return false;
        /* A signal cut the wait short. The key we were waiting for has not
           arrived yet, so go back to waiting rather than reporting one. */
        if (errno != EINTR) return false;
    }
}

bool input_poll(int ms) {
    return wait_readable(ms);
}

key_event_t input_read_key(void) {
    /* Held across calls because one read can bring back more than one key:
       a held-down arrow, or a scripted pty that writes a whole burst. */
    static char buf[KEY_BUF];
    static size_t len = 0;

    for (;;) {
        if (len > 0) {
            key_event_t ev;
            size_t used = input_decode(buf, len, &ev);
            if (used > 0) {
                len -= used;
                memmove(buf, buf + used, len);
                if (ev.type != KEY_IGNORED) return ev;
                continue;
            }
            /* An unfinished sequence with no room left to finish in did not
               come from a keyboard. Dropping it costs one lost keypress;
               keeping it would wedge this loop on a buffer that can never
               decode. */
            if (len == sizeof(buf)) {
                len = 0;
                continue;
            }
        }

        if (!wait_readable(len > 0 ? ESC_TIMEOUT_MS : -1)) {
            /* The only way to be waiting on a timeout at all is a partial
               sequence, and a partial sequence that stopped arriving is what
               a real Esc press looks like. */
            len = 0;
            return event(KEY_ESC, 0);
        }

        ssize_t n = read(STDIN_FILENO, buf + len, sizeof(buf) - len);
        if (n > 0) {
            len += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        /* 0 is stdin closing, which the menu treats the same way it treats
           Ctrl-D: unwind and quit. */
        len = 0;
        return event(KEY_EOF, 0);
    }
}

#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stddef.h>

/* Keypresses as the menu thinks about them, rather than as bytes. A terminal
   sends an arrow key as three or four bytes starting with ESC, and sends a
   lone Esc press as one of them, so the two can only be told apart by whether
   the rest arrives; that is why KEY_NONE exists and why the reader below owns
   a timeout.

   KEY_IGNORED is a key we decoded and have no use for. It is separate from
   KEY_NONE because the bytes were consumed: a function key left undecoded
   would spill its digits into the menu as if someone had typed them. */
typedef enum {
    KEY_NONE = 0,   /* nothing decoded, the caller needs to read more bytes */
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
    KEY_ESC,
    KEY_CHAR,       /* a printable character, in ch */
    KEY_INTERRUPT,  /* Ctrl-C, which arrives as a byte because raw mode drops ISIG */
    KEY_EOF,        /* Ctrl-D, or stdin closing under us */
    KEY_IGNORED
} key_type_t;

typedef struct {
    key_type_t type;
    char ch;        /* the character itself, for KEY_CHAR, and 0 otherwise */
} key_event_t;

/* Decodes the first key in buf and returns how many bytes it took. A return
   of 0 means the buffer holds the start of a sequence and nothing can be
   decided until more arrives, with out set to KEY_NONE. Pure, so the whole
   escape-sequence table is unit tested with no terminal anywhere. */
size_t input_decode(const char *buf, size_t len, key_event_t *out);

/* Blocks until the next key. Only ever returns a key worth acting on, since
   the ones input_decode reports as ignored are dropped here and the read goes
   around again. Requires stdin in raw mode, which render_raw_enter does.

   A lone Esc is reported once the bytes stop coming, so pressing it costs a
   short wait that no other key pays. */
key_event_t input_read_key(void);

/* Whether a byte is waiting to be read within ms milliseconds. select() only,
   never a read, so the byte itself is left untouched for input_read_key to
   decode normally afterward. Used by a screen's open-animation to bail out
   the instant a key is pending, so a held-down arrow never queues frames of
   animation behind it. */
bool input_poll(int ms);

#endif

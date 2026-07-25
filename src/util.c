#include "util.h"
#include <fcntl.h>
#include <unistd.h>

/* Build a random 24-char hex id for a new record. The 12 bytes come straight
   from /dev/urandom, which is present on macOS and Linux without linking a
   crypto library, and gives ids nobody can guess. Record ids double as lookup
   keys, so a predictable one could collide with or expose another row. A short
   read means the kernel handed back fewer random bytes than asked; padding the
   rest would weaken the id, so the whole call fails instead. */
bool id_generate(char out[ID_LEN]) {
    unsigned char raw[12];
    int fd = open("/dev/urandom", O_RDONLY);   // works on macOS and Linux with no extra library
    if (fd < 0) return false;
    ssize_t n = read(fd, raw, sizeof(raw));
    close(fd);
    if (n != (ssize_t)sizeof(raw)) return false;  // a short read means we cannot trust the id
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 12; i++) {
        out[i * 2]     = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 0xF];
    }
    out[24] = '\0';
    return true;
}

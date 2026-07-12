#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "form_util.h"

static void form_url_decode(const char *src, int src_len, char *dst, int dst_size) {
    int i = 0, j = 0;
    while (i < src_len && j < dst_size - 1) {
        if (src[i] == '%' && i + 2 < src_len &&
            isxdigit((unsigned char)src[i + 1]) && isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = { src[i + 1], src[i + 2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

int form_get_all(const char *urlencoded, const char *key, char out[][ID_LEN], int max) {
    if (!urlencoded || !key || max <= 0) return 0;
    size_t keylen = strlen(key);
    const char *p = urlencoded;
    int found = 0;

    while (p && *p && found < max) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!eq) break;

        size_t field_len = (size_t)(eq - p);
        if (field_len == keylen && strncmp(p, key, keylen) == 0) {
            const char *val_start = eq + 1;
            int val_len = amp ? (int)(amp - val_start) : (int)strlen(val_start);
            form_url_decode(val_start, val_len, out[found], ID_LEN);
            found++;
        }

        if (!amp) break;
        p = amp + 1;
    }
    return found;
}

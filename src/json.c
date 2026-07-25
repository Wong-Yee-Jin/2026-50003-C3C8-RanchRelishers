#include "json.h"
#include <stdlib.h>
#include <string.h>

/* Step over the whitespace JSON allows between tokens and return the first
   character that carries meaning. */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Skip a quoted string starting on its opening quote and return the character
   after the closing quote, or NULL when the quote never closes. A backslash
   escapes the next character so an escaped quote does not end the string. */
static const char *skip_string(const char *p) {
    p++;
    while (*p) {
        if (*p == '\\') {
            if (p[1] == '\0') return NULL;
            p += 2;
            continue;
        }
        if (*p == '"') return p + 1;
        p++;
    }
    return NULL;
}

/* Copy a quoted string starting on its opening quote into out, translating the
   backslash escapes JSON defines. Returns the character after the closing
   quote, or NULL on an unterminated string. Output is truncated to fit outlen. */
static const char *copy_string(const char *p, char *out, size_t outlen) {
    size_t i = 0;
    p++;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    /* Decode a \u escape down to a single byte. The GitHub login
                       and repo fields we read are ASCII in practice, so a
                       codepoint that fits becomes that byte and one that will not
                       fit becomes '?'. Widen to full UTF-8 only if some field
                       that can hold non-ASCII ever needs to round-trip. */
                    int v = 0;
                    for (int k = 1; k <= 4; k++) {
                        char h = p[k];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= h - '0';
                        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                        else return NULL;
                    }
                    c = v < 0x80 ? (char)v : '?';
                    p += 4;
                    break;
                }
                case '\0': return NULL;
                default: c = *p; break;
            }
        }
        if (i + 1 < outlen) out[i++] = c;
        p++;
    }
    if (*p != '"') return NULL;
    if (outlen) out[i] = '\0';
    return p + 1;
}

/* Skip one JSON value and return the character after it, or NULL on malformed
   input. Objects and arrays are skipped by counting brackets while stepping
   over strings so a brace inside a string never shifts the depth. */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') return skip_string(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = skip_string(p);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    if (*p == '\0') return NULL;
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    return p;
}

/* Search the object starting on p (its opening brace) for a member named key
   at this object's own level. On a match write the value into out and return
   true. Members whose value is a nested object or array are stepped over with
   skip_value, so a key that lives only inside them is never matched. Returns
   false when the key is absent or the object does not parse. */
static bool object_field(const char *p, const char *key, char *out, size_t outlen) {
    p = skip_ws(p);
    if (*p != '{') return false;
    p++;
    for (;;) {
        p = skip_ws(p);
        if (*p != '"') return false;
        char name[128];
        const char *after = copy_string(p, name, sizeof(name));
        if (!after) return false;
        p = skip_ws(after);
        if (*p != ':') return false;
        p = skip_ws(p + 1);
        if (strcmp(name, key) == 0) {
            if (*p == '"') return copy_string(p, out, outlen) != NULL;
            const char *end = skip_value(p);
            if (!end || end == p) return false;
            /* A non-string value (number, bool, null) is copied as its literal
               text, but clipped to leave room for the terminating NUL so a long
               value can never run past out. */
            size_t n = (size_t)(end - p);
            if (n + 1 > outlen) n = outlen ? outlen - 1 : 0;
            memcpy(out, p, n);
            if (outlen) out[n] = '\0';
            return true;
        }
        const char *end = skip_value(p);
        if (!end) return false;
        p = skip_ws(end);
        if (*p == ',') { p++; continue; }
        return false;
    }
}

/* Public entry point. Reject null or zero-length arguments up front, then hand
   off to the object scanner for the real work. */
bool json_field(const char *body, const char *key, char *out, size_t outlen) {
    if (!body || !key || !out || outlen == 0) return false;
    return object_field(body, key, out, outlen);
}

/* Read a field as a long, falling back to dflt when it is absent or does not
   parse. The endp check catches a field that is empty or starts non-numeric. */
long json_field_int(const char *body, const char *key, long dflt) {
    char buf[64];
    if (!json_field(body, key, buf, sizeof(buf))) return dflt;
    char *endp;
    long v = strtol(buf, &endp, 10);
    if (endp == buf) return dflt;
    return v;
}

/* Walk a top-level array and copy one field from each object element into the
   next out row. A non-object element or one missing the field is skipped rather
   than treated as an error, so one odd entry does not abort the whole scan. */
int json_array_objects(const char *body, const char *field, char out[][128], int max) {
    if (!body || !field || !out || max <= 0) return 0;
    const char *p = skip_ws(body);
    if (*p != '[') return 0;
    p++;
    int count = 0;
    while (count < max) {
        p = skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        if (*p == '{' && object_field(p, field, out[count], 128)) count++;
        const char *end = skip_value(p);
        if (!end) break;
        p = skip_ws(end);
        if (*p == ',') { p++; continue; }
        break;
    }
    return count;
}

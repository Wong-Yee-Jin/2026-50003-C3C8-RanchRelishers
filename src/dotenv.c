#include "dotenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *lstrip(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/* Trims trailing whitespace/newline in place and returns s for chaining. */
static char *rstrip(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}

/* If val is wrapped in a matching pair of quotes, strips them in place. */
static char *unquote(char *val) {
    size_t len = strlen(val);
    if (len >= 2) {
        char first = val[0], last = val[len - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            val[len - 1] = '\0';
            val++;
        }
    }
    return val;
}

void dotenv_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return; /* no .env file: nothing to do, not an error */

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *p = lstrip(line);
        if (*p == '\0' || *p == '#') continue; /* blank line / comment */

        if (strncmp(p, "export", 6) == 0 && isspace((unsigned char)p[6])) {
            p = lstrip(p + 6);
        }

        char *eq = strchr(p, '=');
        if (!eq) continue; /* not a KEY=value line, skip it */
        *eq = '\0';

        char *key = rstrip(p);
        if (*key == '\0') continue;

        char *val = unquote(rstrip(eq + 1));

        setenv(key, val, 0); /* 0 = don't overwrite a var the shell already set */
    }

    fclose(f);
}

#include "ui/menu.h"
#include <stdlib.h>
#include <string.h>

char *ui_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;

    size_t len = strlen(start);
    while (len > 0) {
        char c = start[len - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        start[--len] = '\0';
    }

    /* start may have moved past leading spaces, but the interface promises
       to return s itself, so shift the trimmed text back to the front. */
    if (start != s) memmove(s, start, len + 1);
    return s;
}

int ui_parse_choice(const char *line) {
    if (!line || line[0] == '\0') return -1;
    char *end;
    long v = strtol(line, &end, 10);
    if (*end != '\0') return -1;   // trailing junk means the line wasn't a plain number
    return (int)v;
}

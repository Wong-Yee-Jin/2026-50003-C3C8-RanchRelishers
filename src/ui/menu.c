#include "ui/menu.h"
#include <stdbool.h>
#include <stdio.h>
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

/* Reads one line into buf and trims it, so every screen below gets clean
   input without repeating the fgets/ui_trim pair. Returns false on EOF (a
   piped script running out, or ctrl-D) so callers can unwind instead of
   looping on a line that never arrives. */
static bool read_line(char *buf, size_t n) {
    if (!fgets(buf, (int)n, stdin)) return false;
    ui_trim(buf);
    return true;
}

/* Projects, labels, and assignees screens land in later commits; this menu
   loop already needs somewhere to dispatch to. */
static void screen_projects(void) { printf("projects screen coming soon\n"); }
static void screen_labels(void)   { printf("labels screen coming soon\n"); }
static void screen_assignees(void){ printf("assignees screen coming soon\n"); }

void menu_run(void) {
    char line[64];
    for (;;) {
        printf("\n=== mini-gh-tracker ===\n"
               "1) Projects\n2) Labels\n3) Assignees\n"
               "4) GitHub login\n5) My repos\n0) Quit\n> ");
        if (!read_line(line, sizeof(line))) return;   // stdin closed, act like Quit

        switch (ui_parse_choice(line)) {
            case 1: screen_projects(); break;
            case 2: screen_labels(); break;
            case 3: screen_assignees(); break;
            case 4: printf("GitHub login is available after the M4 login is added.\n"); break;
            case 5: printf("My repos is available after the M4 login is added.\n"); break;
            case 0: return;
            default: printf("unknown choice\n");
        }
    }
}

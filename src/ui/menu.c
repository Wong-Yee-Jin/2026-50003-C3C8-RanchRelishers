#include "ui/menu.h"
#include "core/auth_ctx.h"
#include "core/services.h"
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

/* Labels and assignees screens land in the next commit; this menu loop
   already needs somewhere to dispatch to. */
static void screen_labels(void)   { printf("labels screen coming soon\n"); }
static void screen_assignees(void){ printf("assignees screen coming soon\n"); }

/* Every action funnels its svc_result_t through here so the wording for
   "not signed in" / "not found" / "database error" is written once. Callers
   only supply the text for SVC_INVALID, since that is the one code whose
   meaning changes per action. */
static void print_result(svc_result_t r, const char *invalid_reason) {
    switch (r) {
        case SVC_OK:        break;
        case SVC_DENIED:    printf("sign in first\n"); break;
        case SVC_INVALID:   printf("%s\n", invalid_reason); break;
        case SVC_NOT_FOUND: printf("not found\n"); break;
        case SVC_DB_ERROR:  printf("database error\n"); break;
    }
}

static void print_issue(const issue_t *is) {
    printf("\n#%d %s [%s]\n", is->issue_number, is->title,
           is->status == STATUS_OPEN ? "open" : "closed");
    printf("%s\n", is->description[0] ? is->description : "(no description)");

    /* issue_t only carries label/user ids, so we look each one up in the
       full list to print a name a person can actually read. */
    label_t *labels = NULL;
    int ln = label_service_list(&labels);
    printf("labels:");
    if (is->label_count == 0) printf(" (none)");
    for (int i = 0; i < is->label_count; i++) {
        const char *name = is->label_ids[i];
        for (int j = 0; j < ln; j++) {
            if (strcmp(labels[j].id, is->label_ids[i]) == 0) { name = labels[j].name; break; }
        }
        printf(" %s", name);
    }
    printf("\n");
    free(labels);

    user_t *users = NULL;
    int un = user_service_list(&users);
    printf("assignees:");
    if (is->assignee_count == 0) printf(" (none)");
    for (int i = 0; i < is->assignee_count; i++) {
        const char *name = is->assignee_ids[i];
        for (int j = 0; j < un; j++) {
            if (strcmp(users[j].id, is->assignee_ids[i]) == 0) { name = users[j].username; break; }
        }
        printf(" %s", name);
    }
    printf("\n");
    free(users);

    comment_t *comments = NULL;
    int cn = comment_service_list(is->id, &comments);
    printf("comments (%d):\n", cn);
    for (int i = 0; i < cn; i++) printf("  - %s\n", comments[i].text);
    free(comments);
}

static void screen_issue_detail(const char *issue_id) {
    char line[COMMENT_LEN];
    for (;;) {
        issue_t is;
        if (issue_service_get(issue_id, &is) != SVC_OK) { printf("not found\n"); return; }
        print_issue(&is);
        printf("\nt) toggle status   l) add label   a) assign user   "
               "m) add comment   0) back\n> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 't' || line[0] == 'T') {
            issue_status_t next = is.status == STATUS_OPEN ? STATUS_CLOSED : STATUS_OPEN;
            print_result(issue_service_set_status(issue_id, next), "invalid request");

        } else if (line[0] == 'l' || line[0] == 'L') {
            label_t *labels = NULL;
            int ln = label_service_list(&labels);
            printf("labels:\n");
            for (int i = 0; i < ln; i++) printf("  %d) %s\n", i + 1, labels[i].name);
            printf("label #: ");
            if (read_line(line, sizeof(line))) {
                int idx = ui_parse_choice(line);
                if (idx >= 1 && idx <= ln) {
                    print_result(issue_service_add_label(issue_id, labels[idx - 1].id), "invalid request");
                } else {
                    printf("unknown choice\n");
                }
            }
            free(labels);

        } else if (line[0] == 'a' || line[0] == 'A') {
            user_t *users = NULL;
            int un = user_service_list(&users);
            printf("users:\n");
            for (int i = 0; i < un; i++) printf("  %d) %s\n", i + 1, users[i].username);
            printf("user #: ");
            if (read_line(line, sizeof(line))) {
                int idx = ui_parse_choice(line);
                if (idx >= 1 && idx <= un) {
                    print_result(issue_service_add_assignee(issue_id, users[idx - 1].id), "invalid request");
                } else {
                    printf("unknown choice\n");
                }
            }
            free(users);

        } else if (line[0] == 'm' || line[0] == 'M') {
            printf("comment: ");
            if (read_line(line, sizeof(line))) {
                issue_t parent_check;   // comment_service_add dereferences this unconditionally
                print_result(comment_service_add(issue_id, line, &parent_check), "comment cannot be blank");
            }

        } else if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

static void screen_issues(const char *project_id, const char *project_name) {
    char line[DESC_LEN];
    for (;;) {
        issue_t *issues = NULL;
        int n = issue_service_list(project_id, &issues);
        printf("\n-- Issues: %s --\n", project_name);
        if (n == 0) printf("(no issues yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) #%d [%s] %s\n", i + 1, issues[i].issue_number,
                   issues[i].status == STATUS_OPEN ? "open" : "closed", issues[i].title);
        }
        printf("  c) create   s) search   f) filter   0) back\n> ");
        if (!read_line(line, sizeof(line))) { free(issues); return; }

        if (line[0] == 'c' || line[0] == 'C') {
            free(issues);
            char title[TITLE_LEN], desc[DESC_LEN];
            printf("title: ");
            if (!read_line(title, sizeof(title))) return;
            printf("description: ");
            if (!read_line(desc, sizeof(desc))) return;
            issue_t created;
            print_result(issue_service_create(project_id, title, desc, &created), "title cannot be blank");

        } else if (line[0] == 's' || line[0] == 'S') {
            free(issues);
            char kw[TITLE_LEN];
            printf("keyword: ");
            if (!read_line(kw, sizeof(kw))) return;
            issue_t *results = NULL;
            int rn = issue_service_search(kw, &results);
            printf("-- results for \"%s\" --\n", kw);
            if (rn == 0) printf("(no matches)\n");
            for (int i = 0; i < rn; i++) {
                printf("  #%d [%s] %s\n", results[i].issue_number,
                       results[i].status == STATUS_OPEN ? "open" : "closed", results[i].title);
            }
            free(results);

        } else if (line[0] == 'f' || line[0] == 'F') {
            free(issues);
            char status_in[STATUS_LEN], label_in[ID_LEN];
            printf("status (open/closed, blank for any): ");
            if (!read_line(status_in, sizeof(status_in))) return;
            printf("label id (blank for any): ");
            if (!read_line(label_in, sizeof(label_in))) return;
            /* the filter treats "" as an active filter, so a skipped field
               must cross the service boundary as NULL, not an empty string */
            const char *status_arg = status_in[0] ? status_in : NULL;
            const char *label_arg = label_in[0] ? label_in : NULL;
            issue_t *results = NULL;
            int rn = issue_service_filter(status_arg, label_arg, &results);
            printf("-- filtered issues --\n");
            if (rn == 0) printf("(no matches)\n");
            for (int i = 0; i < rn; i++) {
                printf("  #%d [%s] %s\n", results[i].issue_number,
                       results[i].status == STATUS_OPEN ? "open" : "closed", results[i].title);
            }
            free(results);

        } else {
            int choice = ui_parse_choice(line);
            if (choice == 0) { free(issues); return; }
            if (choice >= 1 && choice <= n) {
                char id[ID_LEN];
                snprintf(id, sizeof(id), "%s", issues[choice - 1].id);
                free(issues);
                screen_issue_detail(id);
            } else {
                printf("unknown choice\n");
                free(issues);
            }
        }
    }
}

static void screen_projects(void) {
    char line[NAME_LEN];
    for (;;) {
        project_t *projects = NULL;
        int n = project_service_list(&projects);
        printf("\n-- Projects --\n");
        if (n == 0) printf("(no projects yet)\n");
        for (int i = 0; i < n; i++) printf("  %d) %s\n", i + 1, projects[i].name);
        printf("  c) create   0) back\n> ");
        if (!read_line(line, sizeof(line))) { free(projects); return; }

        if (line[0] == 'c' || line[0] == 'C') {
            free(projects);
            char name[NAME_LEN];
            printf("project name: ");
            if (!read_line(name, sizeof(name))) return;
            project_t created;
            print_result(project_service_create(name, &created), "blank or duplicate project name");

        } else {
            int choice = ui_parse_choice(line);
            if (choice == 0) { free(projects); return; }
            if (choice >= 1 && choice <= n) {
                char id[ID_LEN], name[NAME_LEN];
                snprintf(id, sizeof(id), "%s", projects[choice - 1].id);
                snprintf(name, sizeof(name), "%s", projects[choice - 1].name);
                free(projects);
                screen_issues(id, name);
            } else {
                printf("unknown choice\n");
                free(projects);
            }
        }
    }
}

void menu_run(void) {
    auth_ctx_set_user("local");   // no GitHub login yet, so the single local user is always signed in
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

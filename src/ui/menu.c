#include "ui/menu.h"
#include "core/github_service.h"
#include "core/services.h"
#include "render.h"
#include "assets.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Strips leading blanks and any trailing whitespace fgets() left on the line
   (the newline, and a trailing \r if input ever comes from a CRLF source). */
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

/* -1 means "not a selectable choice" rather than 0, so a blank line or a
   typo can't be mistaken for the user deliberately picking item 0 (back/quit). */
int ui_parse_choice(const char *line) {
    if (!line || line[0] == '\0') return -1;
    char *end;
    errno = 0;
    long v = strtol(line, &end, 10);
    if (*end != '\0') return -1;   // trailing junk means the line wasn't a plain number
    // a value strtol had to clamp, or one that would not survive the (int)
    // cast below, is not a real menu choice either
    if (errno == ERANGE || v < 0 || v > INT_MAX) return -1;
    return (int)v;
}

/* Reads one line into buf and trims it, so every screen below gets clean
   input without repeating the fgets/ui_trim pair. Returns false on EOF (a
   piped script running out, or ctrl-D) so callers can unwind instead of
   looping on a line that never arrives. */
static bool read_line(char *buf, size_t n) {
    if (!fgets(buf, (int)n, stdin)) return false;
    /* No '\n' in buf means the real line was longer than the buffer, and the
       rest of it is still queued on stdin. Left there, it would answer the
       NEXT prompt instead of this one, so drain it now. Skipped at EOF, since
       there is nothing left to drain and getchar would just report EOF again. */
    if (!strchr(buf, '\n') && !feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    ui_trim(buf);
    return true;
}

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

/* Prints a screen's title, styled to how much room the terminal actually
   has. render_mode() is re-queried on every call rather than cached, so a
   terminal resized between screens picks up the new mode on the next
   screen the user opens instead of staying stuck at whatever mode was
   active at startup. */
static void screen_header(const char *title) {
    render_mode_t mode = render_mode();

    /* On a real terminal the title opens a rule that render_help_row closes
       further down the screen. Everywhere else, including every piped run,
       the plain header below is printed byte for byte as it always was. */
    if (render_decorate()) {
        char banner[TITLE_LEN + 32];
        if (mode == RENDER_FULL) snprintf(banner, sizeof(banner), "%s %s", logo_compact, title);
        else                     snprintf(banner, sizeof(banner), "%s", title);
        printf("\n");
        render_title_rule(banner);
        return;
    }

    switch (mode) {
        case RENDER_FULL:
            printf("\n%s%s%s  %s%s%s\n", render_style(RENDER_ACCENT), logo_compact, render_reset(),
                   render_style(RENDER_ACCENT), title, render_reset());
            break;
        case RENDER_COMPACT:
            printf("\n%s%s%s\n", render_style(RENDER_ACCENT), title, render_reset());
            break;
        case RENDER_MINIMAL:
            printf("\n%s\n%senlarge to 80x24 for the full art%s\n",
                   title, render_style(RENDER_DIM), render_reset());
            break;
    }
}

/* Adds a closed/total bar to a project row. Nothing at all reaches stdout
   unless a terminal is watching, which is what keeps the plain "  1) Name"
   line the e2e suite reads byte for byte. A project with no issues gets no
   bar either: an empty meter says less than the absence of one. */
static void project_progress(const char *project_id) {
    if (!render_decorate()) return;

    issue_t *issues = NULL;
    int n = issue_service_list(project_id, &issues);
    if (n > 0) {
        int closed = 0;
        for (int i = 0; i < n; i++)
            if (issues[i].status == STATUS_CLOSED) closed++;
        char bar[64];
        render_meter(bar, sizeof(bar), closed, n, 12, render_utf8());
        printf("  %s%s%s %d/%d closed",
               render_style(closed == n ? RENDER_OK : RENDER_ACCENT),
               bar, render_reset(), closed, n);
    }
    free(issues);
}

/* Renders one issue in full: header line, description, then labels,
   assignees and comments resolved from the ids stored on the issue into
   names a person reads on screen. */
static void print_issue(const issue_t *is) {
    printf("\n#%d %s [%s]\n", is->issue_number, is->title,
           is->status == STATUS_OPEN ? "open" : "closed");
    printf("%s\n", is->description[0] ? is->description : "(no description)");

    /* issue_t only carries label/user ids, so we look each one up in the
       full list to print a name a person can actually read. */
    label_t *labels = NULL;
    int ln = label_service_list(&labels);
    if (ln < 0) printf("could not read from the database\n");
    printf("labels:");
    if (is->label_count == 0) printf(" (none)");
    for (int i = 0; i < is->label_count; i++) {
        const char *name = is->label_ids[i];
        for (int j = 0; j < ln; j++) {
            if (strcmp(labels[j].id, is->label_ids[i]) == 0) { name = labels[j].name; break; }
        }
        /* Colored off the name, since a label row has no color column of its
           own. Same name, same color, every screen. */
        printf(" %s%s%s", render_style(render_slot_for_label(name)), name, render_reset());
    }
    printf("\n");
    free(labels);

    user_t *users = NULL;
    int un = user_service_list(&users);
    if (un < 0) printf("could not read from the database\n");
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
    if (cn < 0) {
        printf("could not read from the database\n");
    } else {
        printf("comments (%d):\n", cn);
        for (int i = 0; i < cn; i++) printf("  - %s\n", comments[i].text);
    }
    free(comments);
}

/* Action screen for a single issue. Re-fetches the issue at the top of every
   loop pass instead of holding onto the struct across an edit, so a status
   toggle or a new comment shows up on the very next redraw. */
static void screen_issue_detail(const char *issue_id) {
    char line[COMMENT_LEN];
    for (;;) {
        issue_t is;
        if (issue_service_get(issue_id, &is) != SVC_OK) { printf("not found\n"); return; }
        char title[32];
        snprintf(title, sizeof(title), "Issue #%d", is.issue_number);
        screen_header(title);
        print_issue(&is);
        printf("\n");
        render_help_row("t) toggle status   l) add label   a) assign user   "
                        "m) add comment   0) back");
        printf("> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 't' || line[0] == 'T') {
            issue_status_t next = is.status == STATUS_OPEN ? STATUS_CLOSED : STATUS_OPEN;
            print_result(issue_service_set_status(issue_id, next), "invalid request");

        } else if (line[0] == 'l' || line[0] == 'L') {
            label_t *labels = NULL;
            int ln = label_service_list(&labels);
            if (ln < 0) {
                printf("could not read from the database\n");
            } else {
                printf("labels:\n");
                for (int i = 0; i < ln; i++)
                    printf("  %d) %s%s%s\n", i + 1,
                           render_style(render_slot_for_label(labels[i].name)),
                           labels[i].name, render_reset());
                printf("label #: ");
                if (read_line(line, sizeof(line))) {
                    int idx = ui_parse_choice(line);
                    if (idx >= 1 && idx <= ln) {
                        print_result(issue_service_add_label(issue_id, labels[idx - 1].id), "invalid request");
                    } else {
                        printf("unknown choice\n");
                    }
                }
            }
            free(labels);

        } else if (line[0] == 'a' || line[0] == 'A') {
            user_t *users = NULL;
            int un = user_service_list(&users);
            if (un < 0) {
                printf("could not read from the database\n");
            } else {
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

/* Issue list for one project, with create/search/filter and drill-down into
   a single issue. issue_service_list hands back a heap array each pass, so
   every branch below frees it before it returns or recurses, the unknown
   choice branch included, or the array would leak on every mistyped line. */
static void screen_issues(const char *project_id, const char *project_name) {
    char line[DESC_LEN];
    for (;;) {
        issue_t *issues = NULL;
        int n = issue_service_list(project_id, &issues);
        char title[TITLE_LEN + 16];
        snprintf(title, sizeof(title), "Issues: %s", project_name);
        screen_header(title);
        if (n < 0) printf("could not read from the database\n");
        else if (n == 0) printf("(no issues yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) #%d [%s] %s\n", i + 1, issues[i].issue_number,
                   issues[i].status == STATUS_OPEN ? "open" : "closed", issues[i].title);
        }
        render_help_row("  c) create   s) search   f) filter   0) back");
        printf("> ");
        if (!read_line(line, sizeof(line))) { free(issues); return; }

        if (line[0] == 'c' || line[0] == 'C') {
            free(issues);
            char new_title[TITLE_LEN], desc[DESC_LEN];
            printf("title: ");
            if (!read_line(new_title, sizeof(new_title))) return;
            printf("description: ");
            if (!read_line(desc, sizeof(desc))) return;
            issue_t created;
            print_result(issue_service_create(project_id, new_title, desc, &created), "title cannot be blank");

        } else if (line[0] == 's' || line[0] == 'S') {
            free(issues);
            char kw[TITLE_LEN];
            printf("keyword: ");
            if (!read_line(kw, sizeof(kw))) return;
            issue_t *results = NULL;
            int rn = issue_service_search(project_id, kw, &results);
            printf("-- results for \"%s\" --\n", kw);
            if (rn < 0) printf("could not read from the database\n");
            else if (rn == 0) printf("(no matches)\n");
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
            int rn = issue_service_filter(project_id, status_arg, label_arg, &results);
            printf("-- filtered issues --\n");
            if (rn < 0) printf("could not read from the database\n");
            else if (rn == 0) printf("(no matches)\n");
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
                // a bad selection still has to release the list fetched
                // above before the loop goes around and fetches it again
                printf("unknown choice\n");
                free(issues);
            }
        }
    }
}

/* Top-level project list and create screen. Same discipline as the issues
   screen: the project array is freed on every branch, including a stray
   keypress that matches nothing, since it's about to be re-listed anyway. */
static void screen_projects(void) {
    char line[NAME_LEN];
    for (;;) {
        project_t *projects = NULL;
        int n = project_service_list(&projects);
        screen_header("Projects");
        if (n < 0) printf("could not read from the database\n");
        else if (n == 0) printf("(no projects yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) %s", i + 1, projects[i].name);
            project_progress(projects[i].id);
            printf("\n");
        }
        render_help_row("  c) create   0) back");
        printf("> ");
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
                // unrecognized input still exits this pass of the loop, so
                // the list has to go before control gets back to the top
                printf("unknown choice\n");
                free(projects);
            }
        }
    }
}

/* Label list and create screen. Labels have no drill-down screen of their
   own, so the list is freed right after it's printed instead of carrying it
   through the branches below like screen_issues and screen_projects do. */
static void screen_labels(void) {
    char line[NAME_LEN];
    for (;;) {
        label_t *labels = NULL;
        int n = label_service_list(&labels);
        screen_header("Labels");
        if (n < 0) printf("could not read from the database\n");
        else if (n == 0) printf("(no labels yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) %s%s%s", i + 1,
                   render_style(render_slot_for_label(labels[i].name)),
                   labels[i].name, render_reset());
            if (labels[i].description[0]) printf(" - %s", labels[i].description);
            printf("\n");
        }
        free(labels);
        render_help_row("  c) create   0) back");
        printf("> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 'c' || line[0] == 'C') {
            char name[NAME_LEN], desc[LABEL_DESC_LEN];
            printf("label name: ");
            if (!read_line(name, sizeof(name))) return;
            printf("description: ");
            if (!read_line(desc, sizeof(desc))) return;
            label_t created;
            print_result(label_service_create(name, desc, &created), "blank or duplicate label name");
        } else if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

/* Same shape as screen_labels: list what's there, then let a signed-in user
   add to it. Local creation is the only way into the assignee list without
   a network, since GitHub login has nothing to authenticate against offline. */
static void screen_assignees(void) {
    char line[NAME_LEN];
    for (;;) {
        user_t *users = NULL;
        int n = user_service_list(&users);
        screen_header("Assignees");
        /* Starting a session always lands a local user, so an empty list is no
           longer the ordinary empty state: it means that row is not there, and
           the database is not keeping what we write to it. */
        if (n < 0) printf("could not read from the database\n");
        else if (n == 0) printf("(no users found, the database may not be saving)\n");
        for (int i = 0; i < n; i++) printf("  - %s\n", users[i].username);
        free(users);
        render_help_row("  c) create   0) back");
        printf("> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 'c' || line[0] == 'C') {
            char username[USERNAME_LEN];
            printf("username: ");
            if (!read_line(username, sizeof(username))) return;
            user_t created;
            print_result(user_service_create(username, &created), "name taken or empty");
        } else if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

/* Startup identity. The service always leaves us signed in, as a real GitHub
   account when a saved token still works and as the local user otherwise, so
   only the first case is worth announcing: the local session is the ordinary
   way this program runs and saying so on every start would be noise. */
static void github_resume_session(void) {
    if (github_service_resume() == GH_SVC_OK)
        printf("Signed in as %s\n", github_service_username());
}

/* Walks the person through the device flow: show them where to go and what
   to type, then block until the service hears back. The wait is the whole
   menu's wait, which is fine, since there is nothing useful to do here until
   they have finished in the browser anyway. */
static void github_login(void) {
    gh_login_prompt_t prompt;
    if (github_service_login_start(&prompt) != GH_SVC_OK) {
        printf("Set GH_CLIENT_ID and enable device flow on your GitHub OAuth app\n");
        return;
    }
    printf("Open %s and enter code: %s\n", prompt.verification_uri, prompt.user_code);

    bool token_saved = true;
    gh_svc_result_t result = github_service_login_wait(&token_saved);
    /* Printed ahead of the outcome below because it qualifies it: the login
       itself worked, it just will not still be there next start. */
    if (!token_saved)
        printf("warning: could not save the login token, you will need to sign in again next start\n");

    switch (result) {
        case GH_SVC_OK:          printf("Signed in as %s\n", github_service_username()); break;
        case GH_SVC_NO_PROFILE:  printf("signed in, but fetching your GitHub profile failed; try again later\n"); break;
        case GH_SVC_DENIED:      printf("authorization denied\n"); break;
        case GH_SVC_EXPIRED:     printf("code expired, try again\n"); break;
        case GH_SVC_UNREACHABLE: printf("could not reach GitHub, try again\n"); break;
        case GH_SVC_TIMED_OUT:   printf("timed out waiting for authorization, try again\n"); break;
        /* Out of reach from here: we only wait after a start that worked, and
           LOCAL belongs to a session that never opened a flow at all. */
        case GH_SVC_UNAVAILABLE:
        case GH_SVC_LOCAL:       break;
    }
}

/* Drops the saved token and hands the session back to the same local
   identity that startup uses when there was never a token to begin with. */
static void github_logout(void) {
    github_service_logout();
    printf("Logged out of GitHub\n");
}

/* Lists the signed-in user's repos straight from the GitHub API. Needs a
   saved token, so a local-only session is told to log in first rather than
   sent to GitHub with nothing to authenticate the request. */
static void github_repos(void) {
    char names[100][128];
    int n = 0;
    switch (github_service_repos(names, 100, &n)) {
        case GH_SVC_LOCAL:       printf("Sign in with GitHub first (menu item 4)\n"); break;
        case GH_SVC_UNREACHABLE: printf("could not fetch repositories\n"); break;
        case GH_SVC_OK:
            if (n == 0) printf("no repositories\n");
            for (int i = 0; i < n; i++) printf("  - %s\n", names[i]);
            break;
        default: break;   // the login codes never come back from a listing
    }
}

/* Boots the session: play the splash, resume whatever GitHub identity (if
   any) was saved from last time, then loop the main menu until the user
   quits or stdin runs out. */
void menu_run(void) {
    render_splash();
    github_resume_session();
    char line[64];
    for (;;) {
        screen_header("Main Menu");
        printf("1) Projects\n2) Labels\n3) Assignees\n");
        /* An empty username is the local session, which has nothing to log
           out of, so item 4 offers the login instead. */
        const char *gh_user = github_service_username();
        if (gh_user[0]) printf("4) Log out (%s)\n", gh_user);
        else printf("4) GitHub login\n");
        printf("5) My repos\n");
        render_help_row("0) Quit");
        printf("> ");
        if (!read_line(line, sizeof(line))) return;   // stdin closed, act like Quit

        switch (ui_parse_choice(line)) {
            case 1: screen_projects(); break;
            case 2: screen_labels(); break;
            case 3: screen_assignees(); break;
            case 4: gh_user[0] ? github_logout() : github_login(); break;
            case 5: github_repos(); break;
            case 0: return;
            default: printf("unknown choice\n");
        }
    }
}

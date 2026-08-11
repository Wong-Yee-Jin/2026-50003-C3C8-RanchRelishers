#include "ui/menu.h"
#include "core/auth_ctx.h"
#include "core/services.h"
#include "github.h"
#include "token_store.h"
#include "render.h"
#include "assets.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    switch (render_mode()) {
        case RENDER_FULL:
            printf("\n%s%s%s  %s%s%s\n", render_accent(), logo_compact, render_reset(),
                   render_accent(), title, render_reset());
            break;
        case RENDER_COMPACT:
            printf("\n%s%s%s\n", render_accent(), title, render_reset());
            break;
        case RENDER_MINIMAL:
            printf("\n%s\n%senlarge to 80x24 for the full art%s\n",
                   title, render_dim(), render_reset());
            break;
    }
}

/* Renders one issue in full: header line, description, then labels and
   assignees resolved from the ids stored on the issue into names a person
   reads on screen. */
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
}

/* Action screen for a single issue. Re-fetches the issue at the top of every
   loop pass instead of holding onto the struct across an edit, so a status
   toggle shows up on the very next redraw. Labels and assignees are set
   once at issue-creation time (see prompt_labels_and_assignees) and shown
   here read-only; the only action left on this screen is toggling
   open/closed -- there is no comments feature anymore. */
static void screen_issue_detail(const char *issue_id) {
    char line[64]; /* just needs to hold "t"/"0"/junk, not free text anymore */
    for (;;) {
        issue_t is;
        if (issue_service_get(issue_id, &is) != SVC_OK) { printf("not found\n"); return; }
        char title[32];
        snprintf(title, sizeof(title), "Issue #%d", is.issue_number);
        screen_header(title);
        print_issue(&is);
        printf("\nt) toggle status   0) back\n> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 't' || line[0] == 'T') {
            issue_status_t next = is.status == STATUS_OPEN ? STATUS_CLOSED : STATUS_OPEN;
            print_result(issue_service_set_status(issue_id, next), "invalid request");

        } else if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

/* Parses a "1 3 5" / "1,3,5" style multi-select line into 1-based indices,
   writing up to max of them into out and returning how many were found.
   Anything that doesn't parse as a positive number is silently skipped
   rather than aborting the whole line, so one typo doesn't throw away the
   rest of a person's picks. */
static int parse_index_list(const char *line, int *out, int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end != p && v > 0) out[n++] = (int)v;
        p = end;
    }
    return n;
}

/* Offers to attach label(s) and assignee(s) to a just-created issue, right
   in the creation flow -- there's no separate "add label" / "assign user"
   screen anymore, so this is the only place either ever gets set. Blank
   input skips a section entirely; unknown indices are just skipped. */
static void prompt_labels_and_assignees(const char *issue_id) {
    label_t *labels = NULL;
    int ln = label_service_list(&labels);
    if (ln > 0) {
        printf("\nlabels:\n");
        for (int i = 0; i < ln; i++) {
            printf("  %d) %s", i + 1, labels[i].name);
            if (labels[i].description[0]) printf(" - %s", labels[i].description);
            printf("\n");
        }
        char line[256];
        printf("add label #s (space/comma separated, blank for none): ");
        if (read_line(line, sizeof(line)) && line[0]) {
            int idxs[MAX_LABELS];
            int n = parse_index_list(line, idxs, MAX_LABELS);
            for (int i = 0; i < n; i++) {
                if (idxs[i] >= 1 && idxs[i] <= ln)
                    issue_service_add_label(issue_id, labels[idxs[i] - 1].id);
            }
        }
    }
    free(labels);

    user_t *users = NULL;
    int un = user_service_list(&users);
    if (un > 0) {
        printf("\nassignees:\n");
        for (int i = 0; i < un; i++) printf("  %d) %s\n", i + 1, users[i].username);
        char line[256];
        printf("assign user #s (space/comma separated, blank for none): ");
        if (read_line(line, sizeof(line)) && line[0]) {
            int idxs[MAX_ASSIGNEES];
            int n = parse_index_list(line, idxs, MAX_ASSIGNEES);
            for (int i = 0; i < n; i++) {
                if (idxs[i] >= 1 && idxs[i] <= un)
                    issue_service_add_assignee(issue_id, users[idxs[i] - 1].id);
            }
        }
    }
    free(users);
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
        if (n == 0) printf("(no issues yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) #%d [%s] %s\n", i + 1, issues[i].issue_number,
                   issues[i].status == STATUS_OPEN ? "open" : "closed", issues[i].title);
        }
        printf("  c) create   s) search   0) back\n> ");
        if (!read_line(line, sizeof(line))) { free(issues); return; }

        if (line[0] == 'c' || line[0] == 'C') {
            free(issues);
            char new_title[TITLE_LEN], desc[DESC_LEN];
            printf("title: ");
            if (!read_line(new_title, sizeof(new_title))) return;
            printf("description: ");
            if (!read_line(desc, sizeof(desc))) return;
            issue_t created;
            svc_result_t r = issue_service_create(project_id, new_title, desc, &created);
            print_result(r, "title cannot be blank");
            if (r == SVC_OK) prompt_labels_and_assignees(created.id);

        } else if (line[0] == 's' || line[0] == 'S') {
            free(issues);
            char kw[TITLE_LEN];
            printf("keyword: ");
            if (!read_line(kw, sizeof(kw))) return;
            issue_t *results = NULL;
            int rn = issue_service_search(project_id, kw, &results);
            printf("-- results for \"%s\" --\n", kw);
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
                // a bad selection still has to release the list fetched
                // above before the loop goes around and fetches it again
                printf("unknown choice\n");
                free(issues);
            }
        }
    }
}

/* Pulls the signed-in GitHub user's repos -- public and private alike,
   github_list_repos already asks GitHub for everything the token can see --
   and adds any repo name not already tracked as a project. Called once on
   every visit to the Projects screen, so the list stays current with
   GitHub without a separate "My repos" screen. A no-op without a saved
   token, so local-only usage is unaffected; a name that's already a
   project just comes back SVC_INVALID from project_service_create and is
   silently skipped. */
static void sync_projects_from_github(void) {
    char token[256];
    if (!token_load(token, sizeof(token))) return;

    char names[100][128];
    int n = github_list_repos(token, names, 100);
    if (n <= 0) return;

    for (int i = 0; i < n; i++) {
        project_t created;
        project_service_create(names[i], &created);
    }
}

/* Top-level project list and create screen. Same discipline as the issues
   screen: the project array is freed on every branch, including a stray
   keypress that matches nothing, since it's about to be re-listed anyway. */
static void screen_projects(void) {
    sync_projects_from_github();
    char line[NAME_LEN];
    for (;;) {
        project_t *projects = NULL;
        int n = project_service_list(&projects);
        screen_header("Projects");
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
                // unrecognized input still exits this pass of the loop, so
                // the list has to go before control gets back to the top
                printf("unknown choice\n");
                free(projects);
            }
        }
    }
}

/* Static label catalog display. Labels have no "create" screen of their own
   anymore -- the full catalog is seeded once at startup (db_labels_seed)
   and every project picks from that same fixed, shared set. */
static void screen_labels(void) {
    char line[NAME_LEN];
    for (;;) {
        label_t *labels = NULL;
        int n = label_service_list(&labels);
        screen_header("Labels");
        if (n == 0) printf("(no labels yet)\n");
        for (int i = 0; i < n; i++) {
            printf("  %d) %s", i + 1, labels[i].name);
            if (labels[i].description[0]) printf(" - %s", labels[i].description);
            printf("\n");
        }
        free(labels);
        printf("  0) back\n> ");
        if (!read_line(line, sizeof(line))) return;

        if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

#define GH_SUGGEST_MAX 5

/* Same shape as screen_labels: list what's there, then let a signed-in user
   add to it. Typing a name and pressing enter doesn't add it outright --
   it searches GitHub for real accounts matching what was typed and shows
   up to GH_SUGGEST_MAX suggestions to pick from, the same "type, see real
   matches, pick one" idea as the web app's live autocomplete, just resolved
   in one round trip instead of on every keystroke since there's no
   keystroke-level event loop in a blocking terminal read. */
static void screen_assignees(void) {
    char line[NAME_LEN];
    for (;;) {
        user_t *users = NULL;
        int n = user_service_list(&users);
        screen_header("Assignees");
        if (n == 0) printf("(no users yet)\n");
        for (int i = 0; i < n; i++) printf("  - %s\n", users[i].username);
        free(users);
        printf("  c) create   0) back\n> ");
        if (!read_line(line, sizeof(line))) return;

        if (line[0] == 'c' || line[0] == 'C') {
            char query[USERNAME_LEN];
            printf("search github username: ");
            if (!read_line(query, sizeof(query))) return;
            if (!query[0]) { printf("unknown choice\n"); continue; }

            char matches[GH_SUGGEST_MAX][128];
            int mn = github_search_usernames(query, matches, GH_SUGGEST_MAX);
            if (mn < 0) {
                printf("could not reach GitHub to search usernames\n");
                continue;
            }
            if (mn == 0) {
                printf("no GitHub accounts matched \"%s\"\n", query);
                continue;
            }

            printf("matches:\n");
            for (int i = 0; i < mn; i++) printf("  %d) %s\n", i + 1, matches[i]);
            printf("pick #: ");
            if (!read_line(line, sizeof(line))) return;
            int idx = ui_parse_choice(line);
            if (idx < 1 || idx > mn) { printf("unknown choice\n"); continue; }

            user_t created;
            print_result(user_service_create(matches[idx - 1], &created), "name taken or empty");
        } else if (ui_parse_choice(line) == 0) {
            return;
        } else {
            printf("unknown choice\n");
        }
    }
}

/* Tracks the GitHub username for the current process, empty when the signed
   in identity is just the local fallback. This is what decides whether item
   4 shows "GitHub login" or "Log out", since auth_ctx itself is authed
   either way (local counts as signed in so the tracker works offline). */
static char gh_username[USERNAME_LEN] = "";

/* A device flow poll should not wait forever on a code nobody entered. This
   caps it around GitHub's own 15 minute expiry at a 5 second interval, with
   room to spare for the slow_down backoff. */
#define GH_MAX_POLLS 180

/* Startup identity: a cached token that still checks out with GitHub upgrades
   the session to that real user. A token GitHub actually rejects (401/403)
   is dropped so we do not keep retrying it; a transport failure (offline, DNS,
   a stalled connection) leaves the token alone, since it says nothing about
   whether the token is still good, and just falls back to local for this
   session. Either way we end up signed in, local when there is no usable
   token, so the tracker never requires a login to be useful. */
static void github_resume_session(void) {
    char token[256];
    if (token_load(token, sizeof(token))) {
        user_t user;
        bool rejected = false;
        if (github_fetch_and_upsert_user(token, &user, &rejected)) {
            auth_ctx_set_user(user.id);
            snprintf(gh_username, sizeof(gh_username), "%s", user.username);
            printf("Signed in as %s\n", user.username);
            return;
        }
        if (rejected) token_clear();
    }
    auth_ctx_set_user("local");
}

/* Runs the device flow end to end: request a code, show it to the user to
   enter on github.com, then poll until they approve it, deny it, or the code
   expires. Blocks the whole menu while it waits, which is fine since there is
   nothing else useful to do until the user has typed the code in anyway. */
static void github_login(void) {
    gh_device_t dev;
    gh_status_t status = github_device_start(&dev);
    if (status == GH_ERROR) {
        printf("Set GH_CLIENT_ID and enable device flow on your GitHub OAuth app\n");
        return;
    }
    printf("Open %s and enter code: %s\n", dev.verification_uri, dev.user_code);

    int interval = dev.interval;
    // a malformed device response could hand back an interval that busy-polls
    // or, cast to unsigned for sleep(), sleeps for close to forever
    if (interval < 1 || interval > 60) interval = 5;

    char token[256];
    int consecutive_errors = 0;
    for (int poll = 0; poll < GH_MAX_POLLS; poll++) {
        sleep((unsigned int)interval);
        gh_status_t poll_status = github_device_poll(dev.device_code, token, sizeof(token));

        if (poll_status == GH_OK) {
            if (!token_save(token))
                printf("warning: could not save the login token, you will need to sign in again next start\n");
            user_t user;
            bool rejected = false;
            if (github_fetch_and_upsert_user(token, &user, &rejected)) {
                auth_ctx_set_user(user.id);
                snprintf(gh_username, sizeof(gh_username), "%s", user.username);
                printf("Signed in as %s\n", user.username);
            } else {
                printf("signed in, but fetching your GitHub profile failed; try again later\n");
            }
            return;
        }
        if (poll_status == GH_SLOW_DOWN) { interval += 5; consecutive_errors = 0; continue; }
        if (poll_status == GH_DENIED) { printf("authorization denied\n"); return; }
        if (poll_status == GH_EXPIRED) { printf("code expired, try again\n"); return; }
        if (poll_status == GH_ERROR) {
            // a momentary network blip should not throw away an in-progress
            // login; only give up after a few in a row
            if (++consecutive_errors >= 3) {
                printf("could not reach GitHub, try again\n");
                return;
            }
            continue;
        }
        consecutive_errors = 0;
        // GH_PENDING: the user has not authorized yet, poll again
    }
    printf("timed out waiting for authorization, try again\n");
}

/* Drops the saved token and hands the session back to the same local
   identity that startup uses when there was never a token to begin with. */
static void github_logout(void) {
    token_clear();
    auth_ctx_set_user("local");   // back to the local identity, still usable offline
    gh_username[0] = '\0';
    printf("Logged out of GitHub\n");
}

/* Boots the session: nudge the terminal to a usable size, play the splash,
   resume whatever GitHub identity (if any) was saved from last time, then
   loop the main menu until the user quits or stdin runs out.
   "Logged in" here means an actual GitHub identity (gh_username set), not
   just the local fallback auth_ctx uses to stay usable offline: someone
   who has never signed in with GitHub sees only "GitHub login" and "Quit",
   since Projects/Labels/Assignees now depend on GitHub as the source of
   truth for repos and matching real usernames. Once signed in, every
   screen is available and item 4 becomes "Log out". "My repos" no longer
   exists as a separate screen -- repos show up automatically in Projects
   (see sync_projects_from_github). */
void menu_run(void) {
    render_request_size();
    render_splash();
    github_resume_session();
    char line[64];
    for (;;) {
        bool logged_in = gh_username[0] != '\0';
        screen_header("Main Menu");
        if (logged_in) {
            printf("1) Projects\n2) Labels\n3) Assignees\n4) Log out (%s)\n0) Quit\n> ", gh_username);
        } else {
            printf("1) GitHub login\n0) Quit\n> ");
        }
        if (!read_line(line, sizeof(line))) return;   // stdin closed, act like Quit

        int choice = ui_parse_choice(line);
        if (logged_in) {
            switch (choice) {
                case 1: screen_projects(); break;
                case 2: screen_labels(); break;
                case 3: screen_assignees(); break;
                case 4: github_logout(); break;
                case 0: return;
                default: printf("unknown choice\n");
            }
        } else {
            switch (choice) {
                case 1: github_login(); break;
                case 0: return;
                default: printf("unknown choice\n");
            }
        }
    }
}

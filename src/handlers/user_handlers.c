#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "models.h"
#include "form_util.h"
#include "auth.h"

/* ---- Manage the user directory (GET shows list+form, POST creates) ---- */
static void handle_users(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    if (strcasecmp(req->method, "POST") == 0) {
        char username[USERNAME_LEN] = {0};
        htttp_form_get(req->body, "username", username, sizeof(username));

        user_t out;
        if (!db_user_create(username, &out)) {
            /* Alternative Flow: invalid/duplicate username */
            sb_t sb; sb_init(&sb);
            sb_append(&sb, "<h1>Users</h1><div class='error'>That username is already "
                            "taken (or was empty).</div><p><a href='/users'>Back</a></p>");
            char *page = render_page("Users", sb.data);
            http_response_html(resp, 400, page);
            free(page); sb_free(&sb);
            return;
        }
        http_response_redirect(resp, "/users");
        return;
    }

    user_t *list; int n = db_user_list(&list);
    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Users</h1>"
                    "<p>Anyone who signs in with <a href='/login'>GitHub</a> shows up here "
                    "automatically. You can also add a teammate manually below (no GitHub "
                    "account needed) so they can be assigned to issues.</p>"
                    "<form method='POST' action='/users'>"
                    "<input name='username' placeholder='Username' required>"
                    "<button type='submit'>Add User</button></form>");
    if (n == 0) {
        sb_append(&sb, "<p>No users yet.</p>");
    } else {
        for (int i = 0; i < n; i++) {
            sb_append(&sb, "<span class='assignee'>");
            if (list[i].avatar_url[0]) {
                sb_append(&sb, "<img src='");
                sb_append_escaped(&sb, list[i].avatar_url);
                sb_append(&sb, "' alt='' style='width:16px;height:16px;border-radius:50%;"
                                "vertical-align:middle;margin-right:.3rem'>");
            }
            sb_append(&sb, "@");
            sb_append_escaped(&sb, list[i].username);
            sb_append(&sb, "</span> ");
        }
    }
    free(list);

    char *page = render_page("Users", sb.data);
    http_response_html(resp, 200, page);
    free(page); sb_free(&sb);
}

/* ---- Assign Assignee(s) to an issue ---- */
static void handle_assign_users(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char user_ids[MAX_ASSIGNEES][ID_LEN];
    int requested = form_get_all(req->body, "user_id", user_ids, MAX_ASSIGNEES);

    int assigned = 0;
    for (int i = 0; i < requested; i++) {
        user_t u;
        if (strlen(user_ids[i]) > 0 && db_user_find_by_id(user_ids[i], &u)) {
            if (db_issue_assign_user(issue_id, user_ids[i])) assigned++;
        }
    }

    if (assigned == 0) {
        /* Alternative Flow: Selected User(s) Do Not Exist / none chosen */
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>");
        sb_append_escaped(&sb, iss.title);
        sb_append(&sb, "</h1><div class='error'>No valid users were selected.</div>"
                        "<p><a href='/issues/");
        sb_append_escaped(&sb, issue_id);
        sb_append(&sb, "'>Back to issue</a></p>");
        char *page = render_page(iss.title, sb.data);
        http_response_html(resp, 400, page);
        free(page); sb_free(&sb);
        return;
    }

    char redirect[64];
    snprintf(redirect, sizeof(redirect), "/issues/%s", issue_id);
    http_response_redirect(resp, redirect);
}

void user_handlers_register(void) {
    router_add("GET",  "/users", handle_users);
    router_add("POST", "/users", handle_users);
    router_add("POST", "/issues/:id/assignees", handle_assign_users);
}

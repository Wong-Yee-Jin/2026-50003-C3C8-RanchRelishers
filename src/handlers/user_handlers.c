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
#include "oauth_github.h"

#define GH_SEARCH_MAX 6

/* Minimal JSON string escaping for the handful of GitHub-supplied
 * fields (login, avatar_url) we echo back as JSON below. Control
 * characters are dropped rather than escaped as \uXXXX since none of
 * these fields legitimately contain them. */
static void json_escape_append(sb_t *sb, const char *text) {
    char buf[2] = {0, 0};
    for (const char *p = text; *p; p++) {
        switch (*p) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n"); break;
            case '\t': sb_append(sb, "\\t"); break;
            default:
                if ((unsigned char)*p >= 0x20) { buf[0] = *p; sb_append(sb, buf); }
        }
    }
}

/* ---- GitHub username autocomplete for the "Add User" form (GET only,
 * JSON) -- backs the dropdown in handle_users() below, mirroring the
 * search-as-you-type list GitHub shows when adding a repo collaborator. ---- */
static void handle_github_user_search(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_json(resp, 401, "[]");
        return;
    }

    char q[128] = {0};
    htttp_form_get(req->query, "q", q, sizeof(q));

    sb_t sb; sb_init(&sb);
    sb_append(&sb, "[");
    if (strlen(q) >= 2) {
        char logins[GH_SEARCH_MAX][GH_LOGIN_LEN];
        char avatars[GH_SEARCH_MAX][GH_AVATAR_LEN];
        int n = oauth_github_search_users(q, logins, avatars, GH_SEARCH_MAX);
        for (int i = 0; i < n; i++) {
            if (i > 0) sb_append(&sb, ",");
            sb_append(&sb, "{\"login\":\"");
            json_escape_append(&sb, logins[i]);
            sb_append(&sb, "\",\"avatar_url\":\"");
            json_escape_append(&sb, avatars[i]);
            sb_append(&sb, "\"}");
        }
    }
    sb_append(&sb, "]");

    http_response_json(resp, 200, sb.data);
    sb_free(&sb);
}

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

        /* Same check GitHub itself does when a repo owner adds a
         * collaborator by username: the login has to actually exist on
         * GitHub before we'll add them here. */
        if (!oauth_github_username_exists(username)) {
            /* Alternative Flow: not a real GitHub username */
            sb_t sb; sb_init(&sb);
            sb_append(&sb, "<h1>Users</h1><div class='error'>&ldquo;");
            sb_append_escaped(&sb, username);
            sb_append(&sb, "&rdquo; isn't a valid GitHub username.</div>"
                            "<p><a href='/users'>Back</a></p>");
            char *page = render_page("Users", sb.data);
            http_response_html(resp, 400, page);
            free(page); sb_free(&sb);
            return;
        }

        user_t out;
        if (!db_user_create(cur.id, username, &out)) {
            /* Alternative Flow: duplicate username */
            sb_t sb; sb_init(&sb);
            sb_append(&sb, "<h1>Users</h1><div class='error'>That username is already "
                            "taken.</div><p><a href='/users'>Back</a></p>");
            char *page = render_page("Users", sb.data);
            http_response_html(resp, 400, page);
            free(page); sb_free(&sb);
            return;
        }
        http_response_redirect(resp, "/users");
        return;
    }

    user_t *list; int n = db_user_list(cur.id, &list);
    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Users</h1>"
                    "<p>Anyone who signs in with <a href='/login'>GitHub</a> shows up here "
                    "automatically. You can also add a teammate manually below by their "
                    "GitHub username -- like adding a collaborator on GitHub, start typing "
                    "to pick a real account, and we double check it exists before adding "
                    "them -- so they can be assigned to issues.</p>"
                    "<div class='gh-userpick'>"
                    "<form method='POST' action='/users'>"
                    "<input name='username' id='gh-username-input' placeholder='Search by GitHub username' "
                    "autocomplete='off' required>"
                    "<button type='submit'>Add User</button>"
                    "</form>"
                    "<div id='gh-username-results' class='gh-dropdown'></div>"
                    "</div>"
                    "<script>(function(){"
                    "var input=document.getElementById('gh-username-input');"
                    "var box=document.getElementById('gh-username-results');"
                    "if(!input||!box)return;"
                    "var timer=null;"
                    "function hide(){box.style.display='none';box.innerHTML='';}"
                    "function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML;}"
                    "input.addEventListener('input',function(){"
                    "var q=input.value.trim();"
                    "clearTimeout(timer);"
                    "if(q.length<2){hide();return;}"
                    "timer=setTimeout(function(){"
                    "fetch('/users/github-search?q='+encodeURIComponent(q)).then(function(r){return r.json();})"
                    ".then(function(list){"
                    "if(!list||!list.length){hide();return;}"
                    "box.innerHTML=list.map(function(u){"
                    "return '<div class=\\'gh-result\\' data-login=\\''+esc(u.login)+'\\'>"
                    "<img src=\\''+esc(u.avatar_url)+'\\' alt=\\'\\'>"
                    "<div><b>'+esc(u.login)+'</b><small>Invite collaborator</small></div></div>';"
                    "}).join('');"
                    "box.style.display='block';"
                    "}).catch(hide);"
                    "},250);"
                    "});"
                    "box.addEventListener('click',function(e){"
                    "var row=e.target.closest('.gh-result');"
                    "if(!row)return;"
                    "input.value=row.getAttribute('data-login');"
                    "hide();input.focus();"
                    "});"
                    "document.addEventListener('click',function(e){"
                    "if(e.target!==input&&!box.contains(e.target))hide();"
                    "});"
                    "})();</script>");
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
    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    project_t proj;
    if (!db_project_find_by_id(iss.project_id, &proj) || strcmp(proj.owner_id, cur.id) != 0) {
        /* Not this account's issue -- don't leak whether it exists. */
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char user_ids[MAX_ASSIGNEES][ID_LEN];
    int requested = form_get_all(req->body, "user_id", user_ids, MAX_ASSIGNEES);

    int assigned = 0;
    for (int i = 0; i < requested; i++) {
        user_t u;
        /* Only allow assigning contributors that belong to this account's
         * own roster, even if someone crafts a request with another
         * account's user id. */
        if (strlen(user_ids[i]) > 0 && db_user_find_by_id(user_ids[i], &u) &&
            strcmp(u.owner_id, cur.id) == 0) {
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
    router_add("GET",  "/users/github-search", handle_github_user_search);
    router_add("POST", "/issues/:id/assignees", handle_assign_users);
}

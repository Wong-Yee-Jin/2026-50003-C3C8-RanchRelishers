#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "models.h"
#include "auth.h"
#include "form_util.h"

static void format_issue_label(const issue_t *iss, char *out, size_t outlen) {
    snprintf(out, outlen, "#%d %s", iss->issue_number, iss->title);
}

/* A single row in the issues column (col2), styled like the projects column: a chevron-terminated link, highlighted when active. */
static void append_issue_row(sb_t *sb, const issue_t *iss, const char *active_issue_id) {
    char label[TITLE_LEN + 16];
    format_issue_label(iss, label, sizeof(label));

    int active = active_issue_id && strcmp(active_issue_id, iss->id) == 0;
    sb_append(sb, "<a class='row issue-row");
    if (active) sb_append(sb, " active");
    sb_append(sb, "' data-name='");
    sb_append_escaped(sb, label);
    sb_append(sb, "' href='/issues/");
    sb_append_escaped(sb, iss->id);
    sb_append(sb, "'><span class='rowtitle'>");
    sb_append_escaped(sb, label);
    sb_append(sb, "</span><span class='chev'>&rsaquo;</span></a>");
}

static void append_issue_card(sb_t *sb, const issue_t *iss) {
    char numbuf[16];
    snprintf(numbuf, sizeof(numbuf), "#%d ", iss->issue_number);

    sb_append(sb, "<div class='card'><a href='/issues/");
    sb_append_escaped(sb, iss->id);
    sb_append(sb, "'>");
    sb_append_escaped(sb, numbuf);
    sb_append_escaped(sb, iss->title);
    sb_append(sb, "</a> - <span class='");
    sb_append(sb, iss->status == STATUS_OPEN ? "open'>Open" : "closed'>Closed");
    sb_append(sb, "</span></div>");
}

/* ---- Create/View Issue---- */
static void handle_project_issues(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    const char *project_id = path_param_get(params, "id");
    project_t proj;
    if (!db_project_find_by_id(project_id, &proj) || strcmp(proj.owner_id, cur.id) != 0) {
        /* Either the project doesn't exist, or it belongs to a different
         * account -- treat both the same so we don't leak which project
         * ids exist for other users. */
        http_response_html(resp, 404, "<h1>Project not found</h1>");
        return;
    }

    if (strcasecmp(req->method, "POST") == 0) {
        char title[TITLE_LEN] = {0}, desc[DESC_LEN] = {0};
        htttp_form_get(req->body, "title", title, sizeof(title));
        htttp_form_get(req->body, "description", desc, sizeof(desc));

        issue_t out;
        if (!db_issue_create(project_id, title, desc, &out)) {
            /* Alternative Flow: Missing Required Information */
            app_shell_opts_t opts = {0};
            opts.active_project_id = project_id;
            opts.active_project_name = proj.name;
            opts.banner_html = "<div class='error'>Title is required to create an issue.</div>";
            char *page = render_app_shell(proj.name, &opts);
            http_response_html(resp, 400, page);
            free(page);
            return;
        }

        /* Apply any Label(s) / Assigned-to picked in the Add Issue modal. */
        char label_ids[MAX_LABELS][ID_LEN];
        int label_n = form_get_all(req->body, "label_id", label_ids, MAX_LABELS);
        for (int i = 0; i < label_n; i++) {
            label_t lbl;
            if (label_ids[i][0] && db_label_find_by_id(label_ids[i], &lbl)) {
                db_issue_assign_label(out.id, label_ids[i]);
            }
        }

        char user_ids[MAX_ASSIGNEES][ID_LEN];
        int user_n = form_get_all(req->body, "user_id", user_ids, MAX_ASSIGNEES);
        for (int i = 0; i < user_n; i++) {
            user_t u;
            /* Only this account's own contributors can be assigned, even if
             * someone crafts a request with another account's user id. */
            if (user_ids[i][0] && db_user_find_by_id(user_ids[i], &u) &&
                strcmp(u.owner_id, cur.id) == 0) {
                db_issue_assign_user(out.id, user_ids[i]);
            }
        }

        char redirect[128];
        snprintf(redirect, sizeof(redirect), "/projects/%s/issues", project_id);
        http_response_redirect(resp, redirect);
        return;
    }

    /* GET: list issues under this project (col2 of the app shell) */
    issue_t *list;
    int n = db_issue_list_by_project(project_id, &list);

    sb_t rows; sb_init(&rows);
    for (int i = 0; i < n; i++) append_issue_row(&rows, &list[i], NULL);
    free(list);

    app_shell_opts_t opts = {0};
    opts.active_project_id = project_id;
    opts.active_project_name = proj.name;
    opts.col2_rows_html = rows.data;
    char *page = render_app_shell(proj.name, &opts);
    http_response_html(resp, 200, page);
    free(page);
    sb_free(&rows);
}

/* ---- UC4: View Issue (details, labels, assignees, time tracking, comments) ----
 * Renders into col3 of the app shell, alongside col1 (projects) and
 * col2 (this issue's project's issue list). Matches the drilldown in
 * the reference screens: "Issue #N" heading, Status, plain
 * "Issue Title:" / "Description:" / "Label(s):" / "Assigned to:"
 * fields, then a full-width Close/Reopen button (purple once reopened).
 * Labels and assignees are set from the Add Issue modal at creation
 * time; this column just displays them read-only. Time tracking and
 * comments (extra functionality this app already had) are still
 * further down the same column. */
static void handle_view_issue(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        /* Alternative Flow: Issue Not Found */
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    project_t proj;
    if (!db_project_find_by_id(iss.project_id, &proj) || strcmp(proj.owner_id, cur.id) != 0) {
        http_response_html(resp, 404, "<h1>Project not found</h1>");
        return;
    }

    char issue_label[TITLE_LEN + 16];
    format_issue_label(&iss, issue_label, sizeof(issue_label));

    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Issue #");
    {
        char n[16]; snprintf(n, sizeof(n), "%d", iss.issue_number);
        sb_append_escaped(&sb, n);
    }
    sb_append(&sb, "</h1>");

    sb_append(&sb, "<div class='field-label'>Issue Title:</div><div>");
    sb_append_escaped(&sb, iss.title);
    sb_append(&sb, "</div>");

    sb_append(&sb, "<p class='");
    sb_append(&sb, iss.status == STATUS_OPEN ? "open'>Status: Open" : "closed'>Status: Closed");
    sb_append(&sb, "</p>");

    sb_append(&sb, "<div class='field-label'>Description:</div><div>");
    sb_append_escaped(&sb, iss.description);
    sb_append(&sb, "</div>");

    /* labels */
    sb_append(&sb, "<div class='field-label'>Label(s):</div><div>");
    if (iss.label_count == 0) {
        sb_append(&sb, "<span class='muted'>None yet</span>");
    }
    for (int i = 0; i < iss.label_count; i++) {
        label_t lbl;
        if (db_label_find_by_id(iss.label_ids[i], &lbl)) {
            sb_append(&sb, "<span class='label'>");
            sb_append_escaped(&sb, lbl.name);
            sb_append(&sb, "</span>");
        }
    }
    sb_append(&sb, "</div>");

    /* assignees */
    sb_append(&sb, "<div class='field-label'>Assigned to:</div><div>");
    if (iss.assignee_count == 0) {
        sb_append(&sb, "<span class='muted'>Nobody yet</span>");
    }
    for (int i = 0; i < iss.assignee_count; i++) {
        user_t u;
        if (db_user_find_by_id(iss.assignee_ids[i], &u)) {
            sb_append(&sb, "<span class='assignee'>@");
            sb_append_escaped(&sb, u.username);
            sb_append(&sb, "</span>");
        }
    }
    sb_append(&sb, "</div>");

    /* Close or Reopen Issue */
    sb_append(&sb, "<form method='POST' action='/issues/");
    sb_append_escaped(&sb, issue_id);
    if (iss.status == STATUS_OPEN) {
        sb_append(&sb, "/close'><button class='btn-wide' type='submit'>Close Issue</button></form>");
    } else {
        sb_append(&sb, "/reopen'><button class='btn-wide btn-purple' type='submit'>Reopen Issue</button></form>");
    }

    sb_t rows; sb_init(&rows);
    {
        issue_t *siblings; int sn = db_issue_list_by_project(iss.project_id, &siblings);
        for (int i = 0; i < sn; i++) append_issue_row(&rows, &siblings[i], issue_id);
        free(siblings);
    }

    app_shell_opts_t opts = {0};
    opts.active_project_id = iss.project_id;
    opts.active_project_name = proj.name;
    opts.active_issue_id = issue_id;
    opts.active_issue_label = issue_label;
    opts.col2_rows_html = rows.data;
    opts.col3_html = sb.data;
    char *page = render_app_shell(issue_label, &opts);
    http_response_html(resp, 200, page);
    free(page);
    sb_free(&sb);
    sb_free(&rows);
}

/* ---- Close Issue ---- */
static void handle_close_issue(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req;
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }
    if (iss.status == STATUS_OPEN) {
        db_issue_set_status(issue_id, STATUS_CLOSED);
    }
    /* Alternative Flow "Issue Already Closed" */
    char redirect[64];
    snprintf(redirect, sizeof(redirect), "/issues/%s", issue_id);
    http_response_redirect(resp, redirect);
}

/* ---- Reopen Issue ---- */
static void handle_reopen_issue(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req;
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }
    if (iss.status == STATUS_CLOSED) {
        db_issue_set_status(issue_id, STATUS_OPEN);
    }
    char redirect[64];
    snprintf(redirect, sizeof(redirect), "/issues/%s", issue_id);
    http_response_redirect(resp, redirect);
}

/* ---- Search Issues ---- */
static void handle_search_issues(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    char q[TITLE_LEN] = {0};
    htttp_form_get(req->query, "q", q, sizeof(q));

    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Search Issues</h1>"
                    "<form method='GET' action='/issues/search'>"
                    "<input name='q' placeholder='keyword' value='");
    sb_append_escaped(&sb, q);
    sb_append(&sb, "'><button type='submit'>Search</button></form>");

    if (strlen(q) > 0) {
        issue_t *list; int n = db_issue_search(q, &list);
        if (n == 0) {
            /* Alternative Flow: No Matching Results */
            sb_append(&sb, "<p>No issues match your search query.</p>");
        } else {
            for (int i = 0; i < n; i++) append_issue_card(&sb, &list[i]);
        }
        free(list);
    }

    char *page = render_page("Search", sb.data);
    http_response_html(resp, 200, page);
    free(page); sb_free(&sb);
}

/* ---- ADDITIONAL FEATURE: Filter Issues ---- */
static void handle_filter_issues(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    char status[STATUS_LEN] = {0}, label_id[ID_LEN] = {0};
    htttp_form_get(req->query, "status", status, sizeof(status));
    htttp_form_get(req->query, "label", label_id, sizeof(label_id));

    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Filter Issues</h1>"
                    "<form method='GET' action='/issues/filter'>"
                    "<select name='status'><option value=''>Any status</option>"
                    "<option value='open'>Open</option><option value='closed'>Closed</option></select>");

    label_t *labels; int ln = db_label_list(&labels);
    sb_append(&sb, "<select name='label'><option value=''>Any label</option>");
    for (int i = 0; i < ln; i++) {
        sb_append(&sb, "<option value='");
        sb_append_escaped(&sb, labels[i].id);
        sb_append(&sb, "'>");
        sb_append_escaped(&sb, labels[i].name);
        sb_append(&sb, "</option>");
    }
    free(labels);
    sb_append(&sb, "</select><button type='submit'>Filter</button></form>");

    if (strlen(status) > 0 || strlen(label_id) > 0) {
        issue_t *list;
        int n = db_issue_filter(strlen(status) ? status : NULL,
                                 strlen(label_id) ? label_id : NULL, &list);
        if (n == 0) {
            /* Alternative Flow: No Matching Issues */
            sb_append(&sb, "<p>No issues satisfy the selected filter.</p>");
        } else {
            for (int i = 0; i < n; i++) append_issue_card(&sb, &list[i]);
        }
        free(list);
    }

    char *page = render_page("Filter", sb.data);
    http_response_html(resp, 200, page);
    free(page); sb_free(&sb);
}

void issue_handlers_register(void) {
    router_add("GET",  "/projects/:id/issues", handle_project_issues);
    router_add("POST", "/projects/:id/issues", handle_project_issues);
    router_add("GET",  "/issues/search", handle_search_issues);
    router_add("GET",  "/issues/filter", handle_filter_issues);
    router_add("GET",  "/issues/:id", handle_view_issue);
    router_add("POST", "/issues/:id/close", handle_close_issue);
    router_add("POST", "/issues/:id/reopen", handle_reopen_issue);
}

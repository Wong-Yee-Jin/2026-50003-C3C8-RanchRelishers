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

/* ---- View Labels (Fixed catalog) ----*/
static void handle_labels(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req; (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    label_t *list; int n = db_label_list(&list);
    sb_t sb; sb_init(&sb);
    sb_append(&sb, "<h1>Labels</h1>"
                    "<p>This is a fixed set of labels shared by every project; "
                    "assign the ones you need from an issue's page.</p>");
    if (n == 0) {
        sb_append(&sb, "<p>No labels available.</p>");
    } else {
        for (int i = 0; i < n; i++) {
            sb_append(&sb, "<div class='card'><span class='label'>");
            sb_append_escaped(&sb, list[i].name);
            sb_append(&sb, "</span> ");
            sb_append_escaped(&sb, list[i].description);
            sb_append(&sb, "</div>");
        }
    }
    free(list);

    char *page = render_page("Labels", sb.data);
    http_response_html(resp, 200, page);
    free(page); sb_free(&sb);
}

/* ---- Assign Label(s) ---- */
static void handle_assign_label(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char label_ids[MAX_LABELS][ID_LEN];
    int requested = form_get_all(req->body, "label_id", label_ids, MAX_LABELS);

    int assigned = 0;
    for (int i = 0; i < requested; i++) {
        label_t lbl;
        if (strlen(label_ids[i]) > 0 && db_label_find_by_id(label_ids[i], &lbl)) {
            if (db_issue_assign_label(issue_id, label_ids[i])) assigned++;
        }
    }

    if (assigned == 0) {
        /* Alternative Flow: Selected Label(s) Do Not Exist / none chosen */
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>");
        sb_append_escaped(&sb, iss.title);
        sb_append(&sb, "</h1><div class='error'>No valid labels were selected.</div>"
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

void label_handlers_register(void) {
    router_add("GET",  "/labels", handle_labels);
    router_add("POST", "/issues/:id/labels", handle_assign_label);
}

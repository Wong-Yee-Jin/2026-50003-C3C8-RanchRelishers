#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "models.h"

/* ---- UC5: Add Comment ---- */
static void handle_add_comment(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char text[COMMENT_LEN] = {0};
    htttp_form_get(req->body, "text", text, sizeof(text));

    if (!db_comment_add(issue_id, text)) {
        /* Alternative Flow: Empty Comment */
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>");
        sb_append_escaped(&sb, iss.title);
        sb_append(&sb, "</h1><div class='error'>Comment cannot be empty.</div>"
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

void comment_handlers_register(void) {
    router_add("POST", "/issues/:id/comments", handle_add_comment);
}

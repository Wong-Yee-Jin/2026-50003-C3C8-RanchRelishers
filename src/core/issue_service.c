#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* db_issue_search/filter take a fixed page size so no caller can force an
   unbounded scan through the menu or a later API. */
#define ISSUE_LIST_LIMIT 200

/* Filing an issue needs a signed-in reporter and an actual title. The
   description can stay blank, since not every issue needs detail up front. */
svc_result_t issue_service_create(const char *project_id, const char *title,
                                  const char *desc, issue_t *out) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!title || title[0] == '\0') return SVC_INVALID;
    return db_issue_create(project_id, title, desc, out) ? SVC_OK : SVC_DB_ERROR;
}

// A project's issue list is a read, open to anyone browsing the menu.
int issue_service_list(const char *project_id, issue_t **out) {
    return db_issue_list_by_project(project_id, out);
}

// Fetching one issue's detail view is also a read.
svc_result_t issue_service_get(const char *id, issue_t *out) {
    return db_issue_find_by_id(id, out) ? SVC_OK : SVC_NOT_FOUND;
}

// Moving an issue between open and closed changes its real state, so it
// goes through the same auth gate as every other write below.
svc_result_t issue_service_set_status(const char *id, issue_status_t status) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_set_status(id, status) ? SVC_OK : SVC_DB_ERROR;
}

// Attaching a label edits the issue record, so the caller has to be signed in.
svc_result_t issue_service_add_label(const char *issue_id, const char *label_id) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_assign_label(issue_id, label_id) ? SVC_OK : SVC_DB_ERROR;
}

// Assigning a user is the same kind of mutation as adding a label above.
svc_result_t issue_service_add_assignee(const char *issue_id, const char *user_id) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_assign_user(issue_id, user_id) ? SVC_OK : SVC_DB_ERROR;
}

// Keyword search is read-only, so it just forwards to the db layer with the
// shared page limit applied.
int issue_service_search(const char *project_id, const char *keyword, issue_t **out) {
    return db_issue_search(project_id, keyword, ISSUE_LIST_LIMIT, out);
}

// Filtering by status and label is a separate read path from search above:
// status and label are each optional, so passing NULL for one just widens
// the match instead of narrowing it.
int issue_service_filter(const char *project_id, const char *status, const char *label_id, issue_t **out) {
    return db_issue_filter(project_id, status, label_id, ISSUE_LIST_LIMIT, out);
}

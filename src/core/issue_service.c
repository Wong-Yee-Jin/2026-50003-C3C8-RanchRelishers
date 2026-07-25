#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* db_issue_search/filter take a fixed page size so no caller can force an
   unbounded scan through the menu or a later API. */
#define ISSUE_LIST_LIMIT 200

svc_result_t issue_service_create(const char *project_id, const char *title,
                                  const char *desc, issue_t *out) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!title || title[0] == '\0') return SVC_INVALID;
    return db_issue_create(project_id, title, desc, out) ? SVC_OK : SVC_DB_ERROR;
}

int issue_service_list(const char *project_id, issue_t **out) {
    return db_issue_list_by_project(project_id, out);
}

svc_result_t issue_service_get(const char *id, issue_t *out) {
    return db_issue_find_by_id(id, out) ? SVC_OK : SVC_NOT_FOUND;
}

svc_result_t issue_service_set_status(const char *id, issue_status_t status) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_set_status(id, status) ? SVC_OK : SVC_DB_ERROR;
}

svc_result_t issue_service_add_label(const char *issue_id, const char *label_id) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_assign_label(issue_id, label_id) ? SVC_OK : SVC_DB_ERROR;
}

svc_result_t issue_service_add_assignee(const char *issue_id, const char *user_id) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    return db_issue_assign_user(issue_id, user_id) ? SVC_OK : SVC_DB_ERROR;
}

int issue_service_search(const char *keyword, issue_t **out) {
    return db_issue_search(keyword, ISSUE_LIST_LIMIT, out);
}

int issue_service_filter(const char *status, const char *label_id, issue_t **out) {
    return db_issue_filter(status, label_id, ISSUE_LIST_LIMIT, out);
}

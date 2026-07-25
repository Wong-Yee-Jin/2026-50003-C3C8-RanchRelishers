#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* Adding a comment needs a signed-in author, real text, and an issue that
   actually exists. db_issue_find_by_id doubles as that existence check and
   hands the parent issue back through parent_check, so the caller never has
   to look it up a second time. */
svc_result_t comment_service_add(const char *issue_id, const char *text, issue_t *parent_check) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!text || text[0] == '\0') return SVC_INVALID;
    if (!db_issue_find_by_id(issue_id, parent_check)) return SVC_NOT_FOUND;   // no orphan comments
    return db_comment_add(issue_id, text) ? SVC_OK : SVC_DB_ERROR;
}

// Listing an issue's comments is a read, so it skips the auth gate like the
// other list calls in this layer.
int comment_service_list(const char *issue_id, comment_t **out) {
    return db_comment_list_by_issue(issue_id, out);
}

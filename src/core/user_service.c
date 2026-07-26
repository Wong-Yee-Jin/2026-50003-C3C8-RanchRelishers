#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* Without a network there is no GitHub login to hand out user records, so
   this is the offline door in: a local-only user with just a username.
   Guarded the same as label_service_create, a signed-in caller and a name
   that is both present and not already taken. */
svc_result_t user_service_create(const char *username, user_t *out) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!username || username[0] == '\0') return SVC_INVALID;
    if (db_user_name_exists(username)) return SVC_INVALID;
    return db_user_create(username, out) ? SVC_OK : SVC_DB_ERROR;
}

// Listing user records is a read, open without login same as the other list calls.
int user_service_list(user_t **out) { return db_user_list(out); }

// Same reasoning: fetching one user by id is still just a lookup.
svc_result_t user_service_get(const char *id, user_t *out) {
    return db_user_find_by_id(id, out) ? SVC_OK : SVC_NOT_FOUND;
}

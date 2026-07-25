#include "core/services.h"
#include "db.h"

// This service only ever reads user records; accounts get created during
// login, not here, so there's no mutation and no need for the auth_ctx include.
int user_service_list(user_t **out) { return db_user_list(out); }

// Same reasoning: fetching one user by id is still just a lookup.
svc_result_t user_service_get(const char *id, user_t *out) {
    return db_user_find_by_id(id, out) ? SVC_OK : SVC_NOT_FOUND;
}

#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* Labels are shared across every project, so a duplicate name would leave
   the filter menu unable to tell which "bug" label a user meant. Creating
   one mutates shared state, so it needs a signed-in caller like any write. */
svc_result_t label_service_create(const char *name, const char *desc, label_t *out) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!name || name[0] == '\0') return SVC_INVALID;
    if (db_label_name_exists(name)) return SVC_INVALID;
    return db_label_create(name, desc, out) ? SVC_OK : SVC_DB_ERROR;
}

// Listing labels is a read, open without login same as the other list calls.
int label_service_list(label_t **out) { return db_label_list(out); }

#include "core/services.h"
#include "core/auth_ctx.h"
#include "db.h"

/* Only a signed-in caller can add a project, and the name has to be real and
   unique so two entries never collide in the menu list. */
svc_result_t project_service_create(const char *name, project_t *out) {
    if (!auth_ctx_is_authed()) return SVC_DENIED;
    if (!name || name[0] == '\0') return SVC_INVALID;   // a project must be named
    if (db_project_name_exists(name)) return SVC_INVALID;
    return db_project_create(name, out) ? SVC_OK : SVC_DB_ERROR;
}

// Browsing the project list is a read, so it never checks auth_ctx: anyone
// at the menu, logged in or not, can see what projects exist.
int project_service_list(project_t **out) { return db_project_list(out); }

// Looking up one project by id is also a read and stays open for the same reason.
svc_result_t project_service_get(const char *id, project_t *out) {
    return db_project_find_by_id(id, out) ? SVC_OK : SVC_NOT_FOUND;
}

#include "test.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

int main(void) {
    db_init(":memory:");
    project_t p;

    auth_ctx_set_user("");                                        // signed out
    CHECK(project_service_create("Backend", &p) == SVC_DENIED);

    auth_ctx_set_user("local-user");                               // signed in
    CHECK(project_service_create("", &p) == SVC_INVALID);          // blank rejected
    CHECK(project_service_create("Backend", &p) == SVC_OK);
    CHECK(project_service_create("Backend", &p) == SVC_INVALID);   // duplicate rejected

    project_t g;
    CHECK(project_service_get(p.id, &g) == SVC_OK);
    CHECK(project_service_get("nope", &g) == SVC_NOT_FOUND);

    project_t *list = NULL;
    int n = project_service_list(&list);
    CHECK(n == 1);
    free(list);

    db_shutdown();
    return TEST_SUMMARY();
}

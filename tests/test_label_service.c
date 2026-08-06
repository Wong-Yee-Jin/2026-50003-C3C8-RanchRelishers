#include "test.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

int main(void) {
    db_init(":memory:");
    label_t l;

    auth_ctx_set_user("");                                              // signed out
    CHECK(label_service_create("bug", "defect", &l) == SVC_DENIED);

    auth_ctx_set_user("local-user");                                     // signed in
    CHECK(label_service_create("", "x", &l) == SVC_INVALID);            // blank rejected
    CHECK(label_service_create("bug", "defect", &l) == SVC_OK);
    CHECK(label_service_create("bug", "defect", &l) == SVC_INVALID);    // duplicate rejected

    label_t *list = NULL;
    int n = label_service_list(&list);
    CHECK(n == 1);
    free(list);

    db_shutdown();
    return TEST_SUMMARY();
}

#include "test.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

int main(void) {
    db_init(":memory:");
    project_t p; db_project_create("P", &p);
    issue_t is; db_issue_create(p.id, "Bug", "", &is);
    issue_t check;

    auth_ctx_set_user("");                                                  // signed out
    CHECK(comment_service_add(is.id, "hello", &check) == SVC_DENIED);

    auth_ctx_set_user("local-user");                                         // signed in
    CHECK(comment_service_add(is.id, "", &check) == SVC_INVALID);           // text required
    CHECK(comment_service_add("missing-issue", "hi", &check) == SVC_NOT_FOUND);  // parent must exist
    CHECK(comment_service_add(is.id, "first note", &check) == SVC_OK);

    comment_t *list = NULL;
    int n = comment_service_list(is.id, &list);
    CHECK(n == 1);
    CHECK_STR(list[0].text, "first note");
    free(list);

    db_shutdown();
    return TEST_SUMMARY();
}

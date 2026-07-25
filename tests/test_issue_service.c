#include "test.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

int main(void) {
    db_init(":memory:");
    project_t p; db_project_create("P", &p);
    issue_t is;

    auth_ctx_set_user("");                                          // signed out
    CHECK(issue_service_create(p.id, "T", "", &is) == SVC_DENIED);

    auth_ctx_set_user("local-user");                                 // signed in
    CHECK(issue_service_create(p.id, "", "", &is) == SVC_INVALID);   // title required
    CHECK(issue_service_create(p.id, "Real", "", &is) == SVC_OK);

    issue_t got;
    CHECK(issue_service_get(is.id, &got) == SVC_OK);
    CHECK(issue_service_get("missing", &got) == SVC_NOT_FOUND);

    issue_t *list = NULL;
    int n = issue_service_list(p.id, &list);
    CHECK(n == 1);
    free(list);

    auth_ctx_set_user("");
    CHECK(issue_service_set_status(is.id, STATUS_CLOSED) == SVC_DENIED);
    auth_ctx_set_user("local-user");
    CHECK(issue_service_set_status(is.id, STATUS_CLOSED) == SVC_OK);
    issue_service_get(is.id, &got);
    CHECK(got.status == STATUS_CLOSED);

    label_t lb; db_label_create("bug", "", &lb);
    auth_ctx_set_user("");
    CHECK(issue_service_add_label(is.id, lb.id) == SVC_DENIED);
    auth_ctx_set_user("local-user");
    CHECK(issue_service_add_label(is.id, lb.id) == SVC_OK);

    user_t u; db_user_create("alice", &u);
    auth_ctx_set_user("");
    CHECK(issue_service_add_assignee(is.id, u.id) == SVC_DENIED);
    auth_ctx_set_user("local-user");
    CHECK(issue_service_add_assignee(is.id, u.id) == SVC_OK);

    issue_t *s = NULL;
    int sn = issue_service_search("Real", &s);       // no auth needed to read
    CHECK(sn == 1);
    free(s);

    issue_t *f = NULL;
    int fn = issue_service_filter("closed", NULL, &f);   // unset label filter passed as NULL
    CHECK(fn == 1);
    free(f);

    db_shutdown();
    return TEST_SUMMARY();
}

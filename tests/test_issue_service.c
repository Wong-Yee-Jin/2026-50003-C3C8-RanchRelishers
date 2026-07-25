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

    // a second project with an issue that would collide with the search and
    // filter below if project scoping is broken
    project_t p2; db_project_create("Q", &p2);
    issue_t other;
    CHECK(issue_service_create(p2.id, "Real also", "", &other) == SVC_OK);
    CHECK(issue_service_set_status(other.id, STATUS_CLOSED) == SVC_OK);

    issue_t *s = NULL;
    int sn = issue_service_search(p.id, "Real", &s);       // no auth needed to read, scoped to p
    CHECK(sn == 1);                                          // "other" matches "Real" too but lives in p2
    free(s);

    issue_t *f = NULL;
    int fn = issue_service_filter(p.id, "closed", NULL, &f);   // unset label filter passed as NULL
    CHECK(fn == 1);                                              // "other" is closed too but scoped out of p
    free(f);

    db_shutdown();
    return TEST_SUMMARY();
}

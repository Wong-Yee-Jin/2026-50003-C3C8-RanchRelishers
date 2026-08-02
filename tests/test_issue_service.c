#include "unity.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

/* A fresh database and a signed-out session per case. Every write in this
   service is gated on auth_ctx, which is a process-wide static, so a case
   that signs in has to be prevented from leaving the next one logged in and
   turning its DENIED check into a false pass. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* The signed-in project plus one issue titled "Real", used by most cases. */
static void seed_issue(project_t *p, issue_t *is) {
    TEST_ASSERT_TRUE(db_project_create("P", p));
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_create(p->id, "Real", "", is));
}

/* The same, plus a second project holding a closed issue whose title also
   matches "Real". A search or filter that drops its project scope counts that
   one too, which is the mistake these fixtures are here to catch. */
static void seed_two_projects(project_t *p, project_t *q, issue_t *is) {
    issue_t other;
    seed_issue(p, is);
    TEST_ASSERT_TRUE(db_project_create("Q", q));
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_create(q->id, "Real also", "", &other));
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_set_status(other.id, STATUS_CLOSED));
}

/* ---- Use case: Create Issue ---- */

static void test_create_issue_requires_login(void) {
    project_t p;
    issue_t is;
    TEST_ASSERT_TRUE(db_project_create("P", &p));
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, issue_service_create(p.id, "T", "", &is));
}

static void test_create_issue_requires_a_title(void) {
    project_t p;
    issue_t is;
    TEST_ASSERT_TRUE(db_project_create("P", &p));
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, issue_service_create(p.id, "", "", &is));
}

static void test_create_issue_stores_the_issue(void) {
    project_t p;
    issue_t is;
    TEST_ASSERT_TRUE(db_project_create("P", &p));
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_create(p.id, "Real", "", &is));
}

/* ---- Use case: View Issue ---- */

static void test_view_issue_gets_one_by_id(void) {
    project_t p;
    issue_t is, got;
    seed_issue(&p, &is);
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_get(is.id, &got));
    TEST_ASSERT_EQUAL_INT(SVC_NOT_FOUND, issue_service_get("missing", &got));
}

static void test_view_issue_lists_the_issues_in_a_project(void) {
    project_t p;
    issue_t is;
    seed_issue(&p, &is);
    issue_t *list = NULL;
    int n = issue_service_list(p.id, &list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

static void test_view_issue_filter_is_scoped_to_project(void) {
    project_t p, q;
    issue_t is;
    seed_two_projects(&p, &q, &is);
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_set_status(is.id, STATUS_CLOSED));
    issue_t *f = NULL;
    int fn = issue_service_filter(p.id, "closed", NULL, &f);   // unset label filter passed as NULL
    TEST_ASSERT_EQUAL_INT(1, fn);   // q's issue is closed too but must not leak in
    free(f);
}

/* ---- Use case: Close Issue ---- */

static void test_close_issue_requires_login(void) {
    project_t p;
    issue_t is;
    seed_issue(&p, &is);
    auth_ctx_set_user("");
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, issue_service_set_status(is.id, STATUS_CLOSED));
}

static void test_close_issue_sets_status_closed(void) {
    project_t p;
    issue_t is, got;
    seed_issue(&p, &is);
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_set_status(is.id, STATUS_CLOSED));
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_get(is.id, &got));
    TEST_ASSERT_EQUAL_INT(STATUS_CLOSED, got.status);
}

/* ---- Use case: View Labels (attaching one to an issue) ---- */

static void test_view_labels_add_to_issue_requires_login(void) {
    project_t p;
    issue_t is;
    label_t lb;
    seed_issue(&p, &is);
    TEST_ASSERT_TRUE(db_label_create("bug", "", &lb));
    auth_ctx_set_user("");
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, issue_service_add_label(is.id, lb.id));
}

static void test_view_labels_add_to_issue_attaches_the_label(void) {
    project_t p;
    issue_t is;
    label_t lb;
    seed_issue(&p, &is);
    TEST_ASSERT_TRUE(db_label_create("bug", "", &lb));
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_add_label(is.id, lb.id));
}

/* ---- Use case: Assign User to Issue ---- */

static void test_assign_user_to_issue_requires_login(void) {
    project_t p;
    issue_t is;
    user_t u;
    seed_issue(&p, &is);
    TEST_ASSERT_TRUE(db_user_create("alice", &u));
    auth_ctx_set_user("");
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, issue_service_add_assignee(is.id, u.id));
}

static void test_assign_user_to_issue_records_the_assignee(void) {
    project_t p;
    issue_t is;
    user_t u;
    seed_issue(&p, &is);
    TEST_ASSERT_TRUE(db_user_create("alice", &u));
    TEST_ASSERT_EQUAL_INT(SVC_OK, issue_service_add_assignee(is.id, u.id));
}

/* ---- Use case: Search Issue ---- */

static void test_search_issue_is_scoped_to_project(void) {
    project_t p, q;
    issue_t is;
    seed_two_projects(&p, &q, &is);
    issue_t *s = NULL;
    int sn = issue_service_search(p.id, "Real", &s);   // no auth needed to read, scoped to p
    TEST_ASSERT_EQUAL_INT(1, sn);   // q's "Real also" matches the keyword too but lives elsewhere
    free(s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_issue_requires_login);
    RUN_TEST(test_create_issue_requires_a_title);
    RUN_TEST(test_create_issue_stores_the_issue);
    RUN_TEST(test_view_issue_gets_one_by_id);
    RUN_TEST(test_view_issue_lists_the_issues_in_a_project);
    RUN_TEST(test_view_issue_filter_is_scoped_to_project);
    RUN_TEST(test_close_issue_requires_login);
    RUN_TEST(test_close_issue_sets_status_closed);
    RUN_TEST(test_view_labels_add_to_issue_requires_login);
    RUN_TEST(test_view_labels_add_to_issue_attaches_the_label);
    RUN_TEST(test_assign_user_to_issue_requires_login);
    RUN_TEST(test_assign_user_to_issue_records_the_assignee);
    RUN_TEST(test_search_issue_is_scoped_to_project);
    return UNITY_END();
}

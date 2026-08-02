#include "unity.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

/* A fresh database and a signed-out session per case, so the denied-when-
   signed-out check cannot be satisfied by a login another case left in the
   process-wide auth_ctx static. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* An issue to hang comments off, since a comment with no parent is not a
   thing the service will accept. */
static void seed_issue(issue_t *out) {
    project_t p;
    TEST_ASSERT_TRUE(db_project_create("P", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Bug", "", out));
}

/* ---- Use case: View Issue (commenting on the one being viewed) ---- */

static void test_view_issue_comment_requires_login(void) {
    issue_t is, check;
    seed_issue(&is);
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, comment_service_add(is.id, "hello", &check));
}

static void test_view_issue_comment_requires_text(void) {
    issue_t is, check;
    seed_issue(&is);
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, comment_service_add(is.id, "", &check));
}

static void test_view_issue_comment_requires_an_existing_issue(void) {
    issue_t check;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_NOT_FOUND, comment_service_add("missing-issue", "hi", &check));
}

static void test_view_issue_comment_is_stored_and_listed(void) {
    issue_t is, check;
    seed_issue(&is);
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, comment_service_add(is.id, "first note", &check));
    comment_t *list = NULL;
    int n = comment_service_list(is.id, &list);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("first note", list[0].text);
    free(list);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_view_issue_comment_requires_login);
    RUN_TEST(test_view_issue_comment_requires_text);
    RUN_TEST(test_view_issue_comment_requires_an_existing_issue);
    RUN_TEST(test_view_issue_comment_is_stored_and_listed);
    return UNITY_END();
}

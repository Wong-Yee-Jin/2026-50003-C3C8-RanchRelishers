#include "unity.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

/* Users reach the database two ways: the GitHub upsert path, and
   user_service_create for someone typed in on the Assignees screen. Only the
   second is a mutation this service owns, so it is the one with an auth gate
   and the validation checked below. The session is reset per case so no test
   inherits a signed-in user from the one before it. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* ---- Use case: Create Local User ---- */

static void test_create_local_user_requires_login(void) {
    user_t u;
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, user_service_create("alice", &u));
}

static void test_create_local_user_rejects_a_blank_username(void) {
    user_t u;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, user_service_create("", &u));
}

static void test_create_local_user_stores_the_user(void) {
    user_t u;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, user_service_create("alice", &u));
    TEST_ASSERT_EQUAL_STRING("alice", u.username);

    /* Readable back through the service, not just returned in the out param. */
    user_t got;
    TEST_ASSERT_EQUAL_INT(SVC_OK, user_service_get(u.id, &got));
    TEST_ASSERT_EQUAL_STRING("alice", got.username);
}

/* The Assignees screen offers no way to tell two identical usernames apart,
   so a repeat has to be refused rather than quietly creating a second row. */
static void test_create_local_user_rejects_a_duplicate_username(void) {
    user_t first, second;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, user_service_create("alice", &first));
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, user_service_create("alice", &second));

    user_t *list = NULL;
    int n = user_service_list(&list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

/* ---- Use case: Sign Up / Log In ---- */

/* The GitHub path is an upsert: the same account signing in twice updates the
   existing row instead of adding another. */
static void test_sign_up_links_a_github_account_then_updates_it(void) {
    user_t u;
    TEST_ASSERT_TRUE(db_user_upsert_github(4242, "alice", "Alice", "", &u));
    TEST_ASSERT_EQUAL_STRING("alice", u.username);

    user_t again;
    TEST_ASSERT_TRUE(db_user_upsert_github(4242, "alice", "Alice Renamed", "", &again));
    TEST_ASSERT_EQUAL_STRING(u.id, again.id);

    user_t *list = NULL;
    int n = user_service_list(&list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

/* ---- Use case: Assign User to Issue ---- */

/* The assignee picker resolves a stored id back to a person, so an unknown id
   has to come back NOT_FOUND rather than a blank-looking user record. */
static void test_assign_user_to_issue_looks_a_user_up_by_id(void) {
    user_t u, got;
    TEST_ASSERT_TRUE(db_user_create("alice", &u));
    TEST_ASSERT_EQUAL_INT(SVC_OK, user_service_get(u.id, &got));
    TEST_ASSERT_EQUAL_STRING("alice", got.username);
    TEST_ASSERT_EQUAL_INT(SVC_NOT_FOUND, user_service_get("missing", &got));
}

static void test_assign_user_to_issue_lists_the_candidates(void) {
    user_t u;
    TEST_ASSERT_TRUE(db_user_create("alice", &u));
    user_t *list = NULL;
    int n = user_service_list(&list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_local_user_requires_login);
    RUN_TEST(test_create_local_user_rejects_a_blank_username);
    RUN_TEST(test_create_local_user_stores_the_user);
    RUN_TEST(test_create_local_user_rejects_a_duplicate_username);
    RUN_TEST(test_sign_up_links_a_github_account_then_updates_it);
    RUN_TEST(test_assign_user_to_issue_looks_a_user_up_by_id);
    RUN_TEST(test_assign_user_to_issue_lists_the_candidates);
    return UNITY_END();
}

#include "unity.h"
#include "core/github_service.h"
#include "core/auth_ctx.h"
#include "core/services.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The device flow needs the live GitHub API and someone typing a code into a
   browser, so what is testable here is the half that runs with no network:
   the local identity the session falls back to, and the calls that refuse
   before they would reach out.

   Two things have to be isolated for that to hold. XDG_CONFIG_HOME points at
   a directory that does not exist, so a real cached token on the machine
   running the tests cannot pull them onto the network; nothing below saves a
   token, so it stays that way. And GH_CLIENT_ID is blanked, which is what
   makes the login refuse locally instead of opening a request. */
void setUp(void) {
    setenv("XDG_CONFIG_HOME", "build/test-github-service-home", 1);
    setenv("GH_CLIENT_ID", "", 1);
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* The signed-in id has to name a row somebody could be assigned to, not a
   placeholder string, or anything that later attributes work to the current
   user is pointing at nothing. */
static void assert_signed_in_as_a_real_user(const char *expect_username) {
    TEST_ASSERT_TRUE(auth_ctx_is_authed());
    user_t u;
    TEST_ASSERT_TRUE(db_user_find_by_id(auth_ctx_user(), &u));
    TEST_ASSERT_EQUAL_STRING(expect_username, u.username);
}

/* ---- Use case: Sign Up / Log In ---- */

/* Starting with no saved token is the normal offline case: the session comes
   up as the local user rather than refusing to let anyone do anything. */
static void test_a_session_without_a_token_signs_in_as_the_local_user(void) {
    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    assert_signed_in_as_a_real_user("local");

    user_t *list = NULL;
    TEST_ASSERT_EQUAL_INT(1, user_service_list(&list));
    free(list);
}

/* Every start would otherwise add another "local" user, and the assignee
   picker would fill up with copies of the same person. */
static void test_a_second_local_session_reuses_the_same_user(void) {
    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    char first_id[ID_LEN];
    snprintf(first_id, sizeof(first_id), "%s", auth_ctx_user());

    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    TEST_ASSERT_EQUAL_STRING(first_id, auth_ctx_user());

    user_t *list = NULL;
    TEST_ASSERT_EQUAL_INT(1, user_service_list(&list));
    free(list);
}

/* A real GitHub account whose login happens to be "local" is somebody else.
   Adopting their row would quietly file an offline session's work under a
   real person's name, so the session has to make its own. */
static void test_a_local_session_ignores_a_github_user_named_local(void) {
    user_t namesake;
    TEST_ASSERT_TRUE(db_user_upsert_github(77, "local", "Local Lookalike", "", &namesake));

    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    TEST_ASSERT_TRUE(strcmp(namesake.id, auth_ctx_user()) != 0);
    assert_signed_in_as_a_real_user("local");

    /* The row we signed in as is the one with no GitHub account behind it. */
    user_t signed_in;
    TEST_ASSERT_TRUE(db_user_find_by_id(auth_ctx_user(), &signed_in));
    TEST_ASSERT_EQUAL_INT64(0, signed_in.github_id);

    user_t *list = NULL;
    TEST_ASSERT_EQUAL_INT(2, user_service_list(&list));   // the namesake, plus our own
    free(list);
}

/* No GitHub session means no username, which is what tells the menu to offer
   a login rather than a logout. */
static void test_a_local_session_has_no_github_username(void) {
    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    TEST_ASSERT_EQUAL_STRING("", github_service_username());
}

/* Without a client id there is no device flow to open, and the refusal has to
   come back before any request goes out. */
static void test_login_without_a_client_id_reports_it_as_unavailable(void) {
    gh_login_prompt_t prompt;
    TEST_ASSERT_EQUAL_INT(GH_SVC_UNAVAILABLE, github_service_login_start(&prompt));
}

/* Waiting on an authorization that was never opened has to come back at once.
   Without the guard this walks into the poll loop and sleeps its way through
   every retry before giving up, which from the menu looks like a hang. */
static void test_waiting_without_a_started_login_returns_immediately(void) {
    bool token_saved = false;
    TEST_ASSERT_EQUAL_INT(GH_SVC_UNAVAILABLE, github_service_login_wait(&token_saved));
    TEST_ASSERT_TRUE(token_saved);   // nothing was saved, so nothing was lost
}

/* ---- Use case: Log Out ---- */

/* Logging out drops the GitHub half of the session and lands back on the same
   local user, still signed in, so the tracker keeps working offline. */
static void test_logout_lands_back_on_the_local_user(void) {
    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_resume());
    char local_id[ID_LEN];
    snprintf(local_id, sizeof(local_id), "%s", auth_ctx_user());

    github_service_logout();
    TEST_ASSERT_EQUAL_STRING("", github_service_username());
    TEST_ASSERT_EQUAL_STRING(local_id, auth_ctx_user());
    assert_signed_in_as_a_real_user("local");

    user_t *list = NULL;
    TEST_ASSERT_EQUAL_INT(1, user_service_list(&list));
    free(list);
}

/* ---- Use case: View Projects ---- */

/* Listing repositories needs a token to authenticate with, so a local session
   is turned away here instead of sending an unauthenticated request. */
static void test_listing_repos_needs_a_github_session(void) {
    char names[4][128];
    int count = -1;
    TEST_ASSERT_EQUAL_INT(GH_SVC_LOCAL, github_service_repos(names, 4, &count));
    TEST_ASSERT_EQUAL_INT(0, count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_session_without_a_token_signs_in_as_the_local_user);
    RUN_TEST(test_a_second_local_session_reuses_the_same_user);
    RUN_TEST(test_a_local_session_ignores_a_github_user_named_local);
    RUN_TEST(test_a_local_session_has_no_github_username);
    RUN_TEST(test_login_without_a_client_id_reports_it_as_unavailable);
    RUN_TEST(test_waiting_without_a_started_login_returns_immediately);
    RUN_TEST(test_logout_lands_back_on_the_local_user);
    RUN_TEST(test_listing_repos_needs_a_github_session);
    return UNITY_END();
}

#include "unity.h"
#include "github.h"

/* Only the pure parse mapping is unit tested here. The device-flow calls need
   the live GitHub API and a browser authorization, so they are exercised by
   the manual checklist rather than a unit test. Nothing here opens a socket or
   a file, so setUp and tearDown have no work to do. */
void setUp(void) {}
void tearDown(void) {}

/* Use case: Sign Up / Log In. A successful poll carries the bearer token. */
static void test_log_in_maps_a_token_response_to_ok(void) {
    char tok[128];
    TEST_ASSERT_EQUAL_INT(GH_OK, github_parse_token_response(
        "{\"access_token\":\"gho_1\",\"token_type\":\"bearer\"}", tok, sizeof(tok)));
    TEST_ASSERT_EQUAL_STRING("gho_1", tok);
}

/* Use case: Sign Up / Log In. These two keep the caller polling rather than
   giving up, so mixing them with a terminal error would abandon a login that
   was still in progress. */
static void test_log_in_maps_pending_and_slow_down_to_retry(void) {
    char tok[128];
    TEST_ASSERT_EQUAL_INT(GH_PENDING, github_parse_token_response(
        "{\"error\":\"authorization_pending\"}", tok, sizeof(tok)));
    TEST_ASSERT_EQUAL_INT(GH_SLOW_DOWN, github_parse_token_response(
        "{\"error\":\"slow_down\"}", tok, sizeof(tok)));
}

/* Use case: Sign Up / Log In. These two end the flow. */
static void test_log_in_maps_expired_and_denied_to_failure(void) {
    char tok[128];
    TEST_ASSERT_EQUAL_INT(GH_EXPIRED, github_parse_token_response(
        "{\"error\":\"expired_token\"}", tok, sizeof(tok)));
    TEST_ASSERT_EQUAL_INT(GH_DENIED, github_parse_token_response(
        "{\"error\":\"access_denied\"}", tok, sizeof(tok)));
}

/* Use case: View Projects. The repo list is what the menu shows after login,
   and each element carries fields we do not want, so the parse has to pick
   out name and skip the rest. */
static void test_view_projects_parses_repo_names(void) {
    const char *repos = "[{\"name\":\"alpha\",\"private\":false},{\"name\":\"beta-svc\",\"private\":true}]";
    char names[10][128];
    int n = github_parse_repo_names(repos, names, 10);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("alpha", names[0]);
    TEST_ASSERT_EQUAL_STRING("beta-svc", names[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_in_maps_a_token_response_to_ok);
    RUN_TEST(test_log_in_maps_pending_and_slow_down_to_retry);
    RUN_TEST(test_log_in_maps_expired_and_denied_to_failure);
    RUN_TEST(test_view_projects_parses_repo_names);
    return UNITY_END();
}

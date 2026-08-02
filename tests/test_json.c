#include "unity.h"
#include "json.h"

/* The scanner is pure: it reads a caller-supplied buffer and writes a
   caller-supplied buffer, so no fixture is needed. */
void setUp(void) {}
void tearDown(void) {}

/* Use case: Sign Up / Log In. This is the first response the device flow has
   to read, and a wrong user_code or verification_uri sends the person to a
   page that cannot authorize them. */
static void test_log_in_reads_the_device_code_response(void) {
    const char *dev =
      "{\"device_code\":\"abc123\",\"user_code\":\"WDJB-MJHT\","
      "\"verification_uri\":\"https://github.com/login/device\",\"interval\":5}";
    char buf[128];
    TEST_ASSERT_TRUE(json_field(dev, "user_code", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("WDJB-MJHT", buf);
    TEST_ASSERT_TRUE(json_field(dev, "verification_uri", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("https://github.com/login/device", buf);
    TEST_ASSERT_EQUAL_INT(5, json_field_int(dev, "interval", 0));
    TEST_ASSERT_FALSE(json_field(dev, "missing", buf, sizeof(buf)));
}

/* Use case: Sign Up / Log In. The token body is what ends the flow. */
static void test_log_in_reads_the_access_token_response(void) {
    const char *tok = "{\"access_token\":\"gho_xY\",\"token_type\":\"bearer\"}";
    char buf[128];
    TEST_ASSERT_TRUE(json_field(tok, "access_token", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("gho_xY", buf);
}

/* Use case: Sign Up / Log In. While the person is still in the browser,
   GitHub answers with an error field instead of a token. */
static void test_log_in_reads_the_pending_error_response(void) {
    const char *pending = "{\"error\":\"authorization_pending\"}";
    char buf[128];
    TEST_ASSERT_TRUE(json_field(pending, "error", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("authorization_pending", buf);
}

/* Use case: View Projects. A top-level array of repo-shaped objects: one
   field per row. */
static void test_view_projects_reads_a_repo_name_array(void) {
    const char *repos = "[{\"name\":\"alpha\"},{\"name\":\"beta-svc\"}]";
    char names[8][128];
    int n = json_array_objects(repos, "name", names, 8);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("alpha", names[0]);
    TEST_ASSERT_EQUAL_STRING("beta-svc", names[1]);
}

/* Proves this is a scanner and not a substring match: nested_key exists in
   the input but only inside a nested object, so a top-level lookup misses it
   while the sibling top-level key is still found. */
static void test_json_field_only_reads_top_level_keys(void) {
    const char *nested =
      "{\"owner\":{\"nested_key\":\"deep\"},\"top_key\":\"visible\"}";
    char buf[128];
    TEST_ASSERT_FALSE(json_field(nested, "nested_key", buf, sizeof(buf)));
    TEST_ASSERT_TRUE(json_field(nested, "top_key", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("visible", buf);
}

/* Malformed input must fail cleanly: no crash, no hang, no OOB read. */
static void test_json_field_rejects_malformed_input(void) {
    char buf[128];
    TEST_ASSERT_FALSE(json_field("{\"a\":\"b", "a", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(json_field("{\"a\":\"\\u12\"}", "a", buf, sizeof(buf)));
}

/* json_array_objects stops at max even when more elements are present. */
static void test_view_projects_repo_array_stops_at_max(void) {
    const char *repos_capped = "[{\"name\":\"a\"},{\"name\":\"b\"},{\"name\":\"c\"}]";
    char names_capped[8][128];
    int n_capped = json_array_objects(repos_capped, "name", names_capped, 2);
    TEST_ASSERT_EQUAL_INT(2, n_capped);
    TEST_ASSERT_EQUAL_STRING("a", names_capped[0]);
    TEST_ASSERT_EQUAL_STRING("b", names_capped[1]);
}

/* A quoted empty string is a valid value distinct from a missing one. */
static void test_json_field_reads_a_quoted_empty_string(void) {
    char buf[128];
    TEST_ASSERT_TRUE(json_field("{\"a\":\"\"}", "a", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* A bare zero-length token (no value between ':' and ',') is malformed. */
static void test_json_field_rejects_a_bare_empty_value(void) {
    char buf[128];
    TEST_ASSERT_FALSE(json_field("{\"a\":,\"b\":1}", "a", buf, sizeof(buf)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_in_reads_the_device_code_response);
    RUN_TEST(test_log_in_reads_the_access_token_response);
    RUN_TEST(test_log_in_reads_the_pending_error_response);
    RUN_TEST(test_view_projects_reads_a_repo_name_array);
    RUN_TEST(test_json_field_only_reads_top_level_keys);
    RUN_TEST(test_json_field_rejects_malformed_input);
    RUN_TEST(test_view_projects_repo_array_stops_at_max);
    RUN_TEST(test_json_field_reads_a_quoted_empty_string);
    RUN_TEST(test_json_field_rejects_a_bare_empty_value);
    return UNITY_END();
}

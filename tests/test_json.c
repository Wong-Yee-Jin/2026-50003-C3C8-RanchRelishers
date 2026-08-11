#include "test.h"
#include "json.h"

int main(void) {
    const char *dev =
      "{\"device_code\":\"abc123\",\"user_code\":\"WDJB-MJHT\","
      "\"verification_uri\":\"https://github.com/login/device\",\"interval\":5}";
    char buf[128];
    CHECK(json_field(dev, "user_code", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "WDJB-MJHT");
    CHECK(json_field(dev, "verification_uri", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "https://github.com/login/device");
    CHECK(json_field_int(dev, "interval", 0) == 5);
    CHECK(json_field(dev, "missing", buf, sizeof(buf)) == false);

    const char *tok = "{\"access_token\":\"gho_xY\",\"token_type\":\"bearer\"}";
    CHECK(json_field(tok, "access_token", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "gho_xY");

    const char *pending = "{\"error\":\"authorization_pending\"}";
    CHECK(json_field(pending, "error", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "authorization_pending");

    /* A top-level array of repo-shaped objects: one field per row. */
    const char *repos = "[{\"name\":\"alpha\"},{\"name\":\"beta-svc\"}]";
    char names[8][128];
    int n = json_array_objects(repos, "name", names, 8);
    CHECK(n == 2);
    CHECK_STR(names[0], "alpha");
    CHECK_STR(names[1], "beta-svc");

    /* Proves this is a scanner and not a substring match: nested_key exists in
       the input but only inside a nested object, so a top-level lookup misses
       it while the sibling top-level key is still found. */
    const char *nested =
      "{\"owner\":{\"nested_key\":\"deep\"},\"top_key\":\"visible\"}";
    CHECK(json_field(nested, "nested_key", buf, sizeof(buf)) == false);
    CHECK(json_field(nested, "top_key", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "visible");

    /* Malformed input must fail cleanly: no crash, no hang, no OOB read. */
    CHECK(json_field("{\"a\":\"b", "a", buf, sizeof(buf)) == false);
    CHECK(json_field("{\"a\":\"\\u12\"}", "a", buf, sizeof(buf)) == false);

    /* json_array_objects stops at max even when more elements are present. */
    const char *repos_capped = "[{\"name\":\"a\"},{\"name\":\"b\"},{\"name\":\"c\"}]";
    char names_capped[8][128];
    int n_capped = json_array_objects(repos_capped, "name", names_capped, 2);
    CHECK(n_capped == 2);
    CHECK_STR(names_capped[0], "a");
    CHECK_STR(names_capped[1], "b");

    /* A quoted empty string is a valid value distinct from a missing one. */
    CHECK(json_field("{\"a\":\"\"}", "a", buf, sizeof(buf)) == true);
    CHECK_STR(buf, "");

    /* A bare zero-length token (no value between ':' and ',') is malformed. */
    CHECK(json_field("{\"a\":,\"b\":1}", "a", buf, sizeof(buf)) == false);

    /* json_array_field_objects: the array of interest lives nested one
       level down under a key, e.g. GitHub's {"items":[...]} search shape,
       not bare at the top level. */
    const char *search =
      "{\"total_count\":2,\"incomplete_results\":false,\"items\":"
      "[{\"login\":\"octocat\",\"id\":1},{\"login\":\"defunkt\",\"id\":2}]}";
    char logins[8][128];
    int ln = json_array_field_objects(search, "items", "login", logins, 8);
    CHECK(ln == 2);
    CHECK_STR(logins[0], "octocat");
    CHECK_STR(logins[1], "defunkt");

    /* A missing array key is zero results, not a crash. */
    CHECK(json_array_field_objects("{\"total_count\":0}", "items", "login", logins, 8) == 0);

    return TEST_SUMMARY();
}

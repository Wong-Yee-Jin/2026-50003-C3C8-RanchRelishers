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

    return TEST_SUMMARY();
}

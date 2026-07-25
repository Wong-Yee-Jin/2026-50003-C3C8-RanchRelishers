#include "test.h"
#include "github.h"

/* Only the pure parse mapping is unit tested here. The device-flow calls need
   the live GitHub API and a browser authorization, so they are exercised by
   the manual checklist rather than a unit test. */
int main(void) {
    char tok[128];
    CHECK(github_parse_token_response("{\"access_token\":\"gho_1\",\"token_type\":\"bearer\"}", tok, sizeof(tok)) == GH_OK);
    CHECK_STR(tok, "gho_1");
    CHECK(github_parse_token_response("{\"error\":\"authorization_pending\"}", tok, sizeof(tok)) == GH_PENDING);
    CHECK(github_parse_token_response("{\"error\":\"slow_down\"}", tok, sizeof(tok)) == GH_SLOW_DOWN);
    CHECK(github_parse_token_response("{\"error\":\"expired_token\"}", tok, sizeof(tok)) == GH_EXPIRED);
    CHECK(github_parse_token_response("{\"error\":\"access_denied\"}", tok, sizeof(tok)) == GH_DENIED);

    const char *repos = "[{\"name\":\"alpha\",\"private\":false},{\"name\":\"beta-svc\",\"private\":true}]";
    char names[10][128];
    int n = github_parse_repo_names(repos, names, 10);
    CHECK(n == 2);
    CHECK_STR(names[0], "alpha");
    CHECK_STR(names[1], "beta-svc");

    return TEST_SUMMARY();
}

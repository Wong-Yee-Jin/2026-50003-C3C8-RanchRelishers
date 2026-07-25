#include "test.h"
#include "db.h"
#include <stdlib.h>

int main(void) {
    CHECK(db_init(":memory:") == true);   // schema applied on a fresh in-memory db

    project_t p;
    CHECK(db_project_create("Backend", &p) == true);
    CHECK(strlen(p.id) == 24);
    CHECK_STR(p.name, "Backend");

    CHECK(db_project_name_exists("Backend") == true);
    CHECK(db_project_name_exists("Missing") == false);

    project_t found;
    CHECK(db_project_find_by_id(p.id, &found) == true);
    CHECK_STR(found.name, "Backend");
    CHECK(db_project_find_by_id("deadbeef", &found) == false);

    db_project_create("Frontend", &(project_t){0});
    project_t *list = NULL;
    int n = db_project_list(&list);
    CHECK(n == 2);
    CHECK(list != NULL);
    free(list);

    label_t lb;
    CHECK(db_label_create("bug", "defect", &lb) == true);
    CHECK(strlen(lb.id) == 24);
    CHECK(db_label_name_exists("bug") == true);
    label_t lf; CHECK(db_label_find_by_id(lb.id, &lf) == true);
    CHECK_STR(lf.description, "defect");
    label_t *ll = NULL; int ln = db_label_list(&ll); CHECK(ln == 1); free(ll);

    user_t u1;
    CHECK(db_user_upsert_github(42, "octocat", "The Octocat", "http://a/x.png", &u1) == true);
    user_t u2;
    CHECK(db_user_upsert_github(42, "octocat2", "Octo Two", "http://a/y.png", &u2) == true);
    CHECK_STR(u1.id, u2.id);                 // same github_id maps to the same row
    CHECK_STR(u2.username, "octocat2");      // fields updated in place
    user_t uf; CHECK(db_user_find_by_github_id(42, &uf) == true);
    CHECK(uf.github_id == 42);
    user_t *ul = NULL; int un = db_user_list(&ul); CHECK(un == 1); free(ul);

    db_shutdown();
    return TEST_SUMMARY();
}

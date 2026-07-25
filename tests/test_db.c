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

    db_shutdown();
    return TEST_SUMMARY();
}

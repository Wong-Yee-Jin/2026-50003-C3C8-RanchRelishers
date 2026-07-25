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

    db_shutdown();
    return TEST_SUMMARY();
}

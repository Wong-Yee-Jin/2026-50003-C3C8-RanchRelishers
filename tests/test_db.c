#include "test.h"
#include "db.h"
#include <stdlib.h>

int main(void) {
    CHECK(db_init(":memory:") == true);   // schema applied on a fresh in-memory db

    project_t p;
    CHECK(db_project_create("Backend", &p) == true);
    CHECK(strlen(p.id) == 24);
    CHECK_STR(p.name, "Backend");

    db_shutdown();
    return TEST_SUMMARY();
}

#include "test.h"
#include "db.h"
#include "core/services.h"
#include <stdlib.h>

/* user_service only reads: accounts are created through the GitHub upsert
   path (M4), not through a local create call, so there is no mutation here
   to gate on auth. */
int main(void) {
    db_init(":memory:");
    user_t u;
    CHECK(db_user_create("alice", &u) == true);

    user_t got;
    CHECK(user_service_get(u.id, &got) == SVC_OK);
    CHECK_STR(got.username, "alice");
    CHECK(user_service_get("missing", &got) == SVC_NOT_FOUND);

    user_t *list = NULL;
    int n = user_service_list(&list);
    CHECK(n == 1);
    free(list);

    db_shutdown();
    return TEST_SUMMARY();
}

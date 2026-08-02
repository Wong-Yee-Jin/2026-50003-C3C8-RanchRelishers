#include "unity.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

/* A fresh database and a signed-out session per case. auth_ctx keeps the
   current user in a process-wide static, so without the reset a case that
   signs in would leave the next one authenticated by accident and the
   denied-when-signed-out checks would stop meaning anything. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* ---- Use case: Create Project ---- */

static void test_create_project_requires_login(void) {
    project_t p;
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, project_service_create("Backend", &p));
}

static void test_create_project_rejects_a_blank_name(void) {
    project_t p;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, project_service_create("", &p));
}

static void test_create_project_stores_the_project(void) {
    project_t p;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, project_service_create("Backend", &p));
}

static void test_create_project_rejects_a_duplicate_name(void) {
    project_t p;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, project_service_create("Backend", &p));
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, project_service_create("Backend", &p));
}

/* ---- Use case: View Projects ---- */

static void test_view_projects_gets_one_by_id(void) {
    project_t p, g;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, project_service_create("Backend", &p));
    TEST_ASSERT_EQUAL_INT(SVC_OK, project_service_get(p.id, &g));
    TEST_ASSERT_EQUAL_INT(SVC_NOT_FOUND, project_service_get("nope", &g));
}

static void test_view_projects_lists_every_project(void) {
    project_t p;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, project_service_create("Backend", &p));
    project_t *list = NULL;
    int n = project_service_list(&list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_project_requires_login);
    RUN_TEST(test_create_project_rejects_a_blank_name);
    RUN_TEST(test_create_project_stores_the_project);
    RUN_TEST(test_create_project_rejects_a_duplicate_name);
    RUN_TEST(test_view_projects_gets_one_by_id);
    RUN_TEST(test_view_projects_lists_every_project);
    return UNITY_END();
}

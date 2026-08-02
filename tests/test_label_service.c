#include "unity.h"
#include "db.h"
#include "core/services.h"
#include "core/auth_ctx.h"
#include <stdlib.h>

/* A fresh database and a signed-out session per case, because auth_ctx holds
   the current user in a process-wide static that would otherwise carry over
   from whichever case signed in last. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));
    auth_ctx_set_user("");
}

void tearDown(void) {
    auth_ctx_set_user("");
    db_shutdown();
}

/* ---- Use case: View Labels ---- */

static void test_view_labels_create_requires_login(void) {
    label_t l;
    TEST_ASSERT_EQUAL_INT(SVC_DENIED, label_service_create("bug", "defect", &l));
}

static void test_view_labels_create_rejects_a_blank_name(void) {
    label_t l;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, label_service_create("", "x", &l));
}

static void test_view_labels_create_stores_the_label(void) {
    label_t l;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, label_service_create("bug", "defect", &l));
}

/* Labels are shared across every project, so a second "bug" would give two
   rows the same meaning and split every filter that uses it. */
static void test_view_labels_create_rejects_a_duplicate_name(void) {
    label_t l;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, label_service_create("bug", "defect", &l));
    TEST_ASSERT_EQUAL_INT(SVC_INVALID, label_service_create("bug", "defect", &l));
}

static void test_view_labels_lists_every_label(void) {
    label_t l;
    auth_ctx_set_user("local-user");
    TEST_ASSERT_EQUAL_INT(SVC_OK, label_service_create("bug", "defect", &l));
    label_t *list = NULL;
    int n = label_service_list(&list);
    TEST_ASSERT_EQUAL_INT(1, n);
    free(list);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_view_labels_create_requires_login);
    RUN_TEST(test_view_labels_create_rejects_a_blank_name);
    RUN_TEST(test_view_labels_create_stores_the_label);
    RUN_TEST(test_view_labels_create_rejects_a_duplicate_name);
    RUN_TEST(test_view_labels_lists_every_label);
    return UNITY_END();
}

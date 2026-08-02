#include "unity.h"
#include "util.h"
#include <ctype.h>
#include <string.h>

/* id_generate only touches the buffer it is handed, so there is no fixture to
   build or tear down here. */
void setUp(void) {}
void tearDown(void) {}

/* Use case: Sign Up / Log In. Every row this app writes takes its primary key
   from here, so a short or non-random id is a problem for every table at once,
   not just for users. */
static void test_sign_up_generates_a_24_char_hex_id(void) {
    char id[ID_LEN];
    TEST_ASSERT_TRUE(id_generate(id));
    TEST_ASSERT_EQUAL_size_t(24, strlen(id));   // 12 random bytes rendered as hex
    for (int i = 0; id[i]; i++) {
        TEST_ASSERT_TRUE(isxdigit((unsigned char)id[i]));
    }
}

static void test_sign_up_generates_a_different_id_each_call(void) {
    char id[ID_LEN], id2[ID_LEN];
    TEST_ASSERT_TRUE(id_generate(id));
    TEST_ASSERT_TRUE(id_generate(id2));
    TEST_ASSERT_TRUE(strcmp(id, id2) != 0);   // two calls do not collide in practice
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sign_up_generates_a_24_char_hex_id);
    RUN_TEST(test_sign_up_generates_a_different_id_each_call);
    return UNITY_END();
}

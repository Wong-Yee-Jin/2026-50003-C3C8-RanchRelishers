#include "unity.h"
#include "ui/menu.h"

/* Both helpers under test are pure string work on a caller-owned buffer, so
   there is nothing to set up. */
void setUp(void) {}
void tearDown(void) {}

static void test_menu_reads_a_numeric_choice(void) {
    char a[] = "  3\n";
    TEST_ASSERT_EQUAL_INT(3, ui_parse_choice(ui_trim(a)));
}

/* A bare Enter or a stray letter has to read as invalid, not as choice 0,
   which would silently pick the first menu entry. */
static void test_menu_rejects_blank_and_non_numeric_input(void) {
    char b[] = "\n";
    TEST_ASSERT_EQUAL_INT(-1, ui_parse_choice(ui_trim(b)));
    char c[] = "x\n";
    TEST_ASSERT_EQUAL_INT(-1, ui_parse_choice(ui_trim(c)));
}

static void test_menu_trims_surrounding_whitespace(void) {
    char d[] = " hi \n";
    TEST_ASSERT_EQUAL_STRING("hi", ui_trim(d));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_reads_a_numeric_choice);
    RUN_TEST(test_menu_rejects_blank_and_non_numeric_input);
    RUN_TEST(test_menu_trims_surrounding_whitespace);
    return UNITY_END();
}

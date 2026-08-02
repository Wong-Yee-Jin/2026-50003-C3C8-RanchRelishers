#include "unity.h"
#include "ui/menu.h"
#include <limits.h>

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

/* A number too big for an int used to wrap around the (int) cast instead of
   getting rejected, so "4294967297" silently landed on choice 1. strtol's
   own ERANGE has to be caught too, since a much longer digit string overflows
   even a 64-bit long before it ever reaches the int range check. */
static void test_menu_rejects_choices_outside_int_range(void) {
    char a[] = "4294967297";
    TEST_ASSERT_EQUAL_INT(-1, ui_parse_choice(a));
    char b[] = "99999999999999999999";
    TEST_ASSERT_EQUAL_INT(-1, ui_parse_choice(b));
    char c[] = "2147483647";
    TEST_ASSERT_EQUAL_INT(INT_MAX, ui_parse_choice(c));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_reads_a_numeric_choice);
    RUN_TEST(test_menu_rejects_blank_and_non_numeric_input);
    RUN_TEST(test_menu_trims_surrounding_whitespace);
    RUN_TEST(test_menu_rejects_choices_outside_int_range);
    return UNITY_END();
}

#include "unity.h"
#include "render.h"
#include "assets.h"
#include <string.h>

/* Both decisions under test are pure functions of their arguments and the art
   is a compile-time constant, so there is no state to reset between cases. */
void setUp(void) {}
void tearDown(void) {}

static void test_render_mode_follows_terminal_size(void) {
    TEST_ASSERT_EQUAL_INT(RENDER_FULL, render_mode_for(80, 24));
    TEST_ASSERT_EQUAL_INT(RENDER_FULL, render_mode_for(120, 40));
    TEST_ASSERT_EQUAL_INT(RENDER_COMPACT, render_mode_for(79, 24));
    TEST_ASSERT_EQUAL_INT(RENDER_COMPACT, render_mode_for(80, 23));
    TEST_ASSERT_EQUAL_INT(RENDER_COMPACT, render_mode_for(40, 10));
    TEST_ASSERT_EQUAL_INT(RENDER_MINIMAL, render_mode_for(39, 24));
}

static void test_render_color_honors_no_color_and_tty(void) {
    TEST_ASSERT_TRUE(render_color_for(true, NULL));
    TEST_ASSERT_TRUE(render_color_for(true, ""));      // empty NO_COLOR means unset
    TEST_ASSERT_FALSE(render_color_for(true, "1"));
    TEST_ASSERT_FALSE(render_color_for(false, NULL));  // not a tty, never color
}

/* Frame-geometry sanity: the art must never wrap or clip in an 80-col
   terminal, and needs enough rows to read as block letters rather than
   a single squished line. */
static void test_wordmark_fits_an_eighty_column_terminal(void) {
    TEST_ASSERT_TRUE(wordmark_height() >= 5);
    for (int i = 0; wordmark[i]; i++) {
        TEST_ASSERT_TRUE((int)strlen(wordmark[i]) <= 80);
        TEST_ASSERT_EQUAL_INT(wordmark_width(), (int)strlen(wordmark[i]));
    }
}

static void test_compact_logo_fits_a_forty_column_terminal(void) {
    TEST_ASSERT_TRUE((int)strlen(logo_compact) <= 40);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_mode_follows_terminal_size);
    RUN_TEST(test_render_color_honors_no_color_and_tty);
    RUN_TEST(test_wordmark_fits_an_eighty_column_terminal);
    RUN_TEST(test_compact_logo_fits_a_forty_column_terminal);
    return UNITY_END();
}

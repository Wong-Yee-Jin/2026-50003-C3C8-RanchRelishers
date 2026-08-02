#include "unity.h"
#include "render.h"
#include "assets.h"
#include <locale.h>
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

/* Screen control answers to the tty alone. NO_COLOR belongs to the color
   decision above and has no say over whether the cursor comes back. */
static void test_render_escapes_follow_the_tty_alone(void) {
    TEST_ASSERT_TRUE(render_escapes_for(true));
    TEST_ASSERT_FALSE(render_escapes_for(false));
}

/* The whole depth heuristic, one case per branch. Each case landing on
   RENDER_COLOR_NONE is a path where the app must emit no color at all. */
static void test_render_depth_reads_colorterm_and_term(void) {
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_TRUE, render_depth_for(true, NULL, "truecolor", "xterm-256color"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_TRUE, render_depth_for(true, NULL, "24bit", "xterm"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_TRUE, render_depth_for(true, NULL, NULL, "xterm-direct"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_256, render_depth_for(true, NULL, NULL, "screen-256color"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_16, render_depth_for(true, NULL, NULL, "xterm"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_16, render_depth_for(true, NULL, "", "vt100"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_NONE, render_depth_for(true, NULL, NULL, "dumb"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_NONE, render_depth_for(true, NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_NONE, render_depth_for(true, NULL, "truecolor", ""));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_NONE, render_depth_for(true, "1", "truecolor", "xterm-256color"));
    TEST_ASSERT_EQUAL_INT(RENDER_COLOR_NONE, render_depth_for(false, NULL, "truecolor", "xterm-256color"));
}

/* One slot at all four depths. The empty string at the bottom is the case the
   e2e suite depends on: piped output must carry no escape bytes. */
static void test_render_style_follows_the_depth(void) {
    char buf[24];
    TEST_ASSERT_EQUAL_STRING("\x1b[38;2;94;200;240m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_TRUE));
    TEST_ASSERT_EQUAL_STRING("\x1b[38;5;117m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_256));
    TEST_ASSERT_EQUAL_STRING("\x1b[96m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_16));
    TEST_ASSERT_EQUAL_STRING("",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_NONE));
    /* Every slot has to produce something at 16 colors, or a call site would
       silently lose its styling on a plain xterm. */
    for (int slot = 0; slot < RENDER_SLOT_COUNT; slot++) {
        render_style_into(buf, sizeof(buf), (render_slot_t)slot, RENDER_COLOR_16);
        TEST_ASSERT_TRUE(buf[0] == '\x1b');
    }
}

/* Known answers for the quantizer, including the greyscale branch and btop's
   quirk that black lands on 232 (near-black) instead of on 16. */
static void test_rgb_to_256_hits_the_right_cube_and_ramp(void) {
    TEST_ASSERT_EQUAL_INT(244, render_rgb_to_256(128, 128, 128));
    TEST_ASSERT_EQUAL_INT(196, render_rgb_to_256(255, 0, 0));
    TEST_ASSERT_EQUAL_INT(232, render_rgb_to_256(0, 0, 0));
    TEST_ASSERT_EQUAL_INT(255, render_rgb_to_256(255, 255, 255));
}

/* A label's color is only useful if it never moves, so the same name must
   land on the same slot every time. DIM is excluded on purpose: a dimmed
   label reads as switched off. */
static void test_label_colors_are_stable_and_never_dim(void) {
    static const char *const names[] = { "bug", "feature", "question", "urgent", "wontfix" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        render_slot_t first = render_slot_for_label(names[i]);
        TEST_ASSERT_EQUAL_INT(first, render_slot_for_label(names[i]));
        TEST_ASSERT_TRUE(first != RENDER_DIM);
        TEST_ASSERT_TRUE(first < RENDER_SLOT_COUNT);
    }
    TEST_ASSERT_TRUE(render_slot_for_label("bug") != render_slot_for_label("feature"));
}

/* Only the two spellings of UTF-8 count. Anything else, including the ASCII
   codeset a bare LANG=C reports, has to fall back to the plain glyphs. */
static void test_utf8_detection_reads_the_codeset(void) {
    TEST_ASSERT_TRUE(render_utf8_for("UTF-8"));
    TEST_ASSERT_TRUE(render_utf8_for("utf8"));
    TEST_ASSERT_FALSE(render_utf8_for("ANSI_X3.4-1968"));
    TEST_ASSERT_FALSE(render_utf8_for("ISO-8859-1"));
    TEST_ASSERT_FALSE(render_utf8_for(""));
    TEST_ASSERT_FALSE(render_utf8_for(NULL));
}

/* ASCII width is just the byte count. The wide case needs a UTF-8 locale to
   decode at all, and not every machine has one installed, so it is skipped
   rather than failed when setlocale turns it down. */
static void test_display_width_counts_columns_not_bytes(void) {
    TEST_ASSERT_EQUAL_INT(0, render_display_width(""));
    TEST_ASSERT_EQUAL_INT(11, render_display_width("gh-tracker "));
    TEST_ASSERT_EQUAL_INT((int)strlen("Issues: ProjA"), render_display_width("Issues: ProjA"));

    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
        setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        TEST_IGNORE_MESSAGE("no UTF-8 locale on this machine");
    }
    /* Three bytes, one codepoint, two columns. A rule sized off strlen would
       overshoot this title by one. */
    TEST_ASSERT_EQUAL_INT(2, render_display_width("\xe4\xb8\xad"));
    TEST_ASSERT_EQUAL_INT(6, render_display_width("ok \xe4\xb8\xad!"));
    /* The box glyphs themselves have to measure one column each, or every
       rule drawn out of them would be three times too long. */
    TEST_ASSERT_EQUAL_INT(1, render_display_width("\xe2\x95\xad"));
    setlocale(LC_CTYPE, "C");
}

/* The two ends and the near miss. 9 of 10 has to stay strictly under a full
   bar, or a project with an open issue left would look finished. */
static void test_meter_fills_by_eighths(void) {
    TEST_ASSERT_EQUAL_INT(0, render_meter_eighths(0, 10, 20));
    TEST_ASSERT_EQUAL_INT(160, render_meter_eighths(10, 10, 20));
    TEST_ASSERT_EQUAL_INT(144, render_meter_eighths(9, 10, 20));
    TEST_ASSERT_EQUAL_INT(80, render_meter_eighths(5, 10, 20));
    TEST_ASSERT_EQUAL_INT(0, render_meter_eighths(3, 0, 20));    // no issues, no division
    TEST_ASSERT_EQUAL_INT(0, render_meter_eighths(3, 10, 0));    // no room to draw
    TEST_ASSERT_TRUE(render_meter_eighths(9999, 10000, 20) < 160);
}

/* The ASCII bar is checked byte for byte because it is the branch a
   non-UTF-8 terminal gets, and it has to stay exactly as wide as asked. */
static void test_meter_draws_a_fixed_width_bar(void) {
    char buf[64];
    render_meter(buf, sizeof(buf), 5, 10, 8, false);
    TEST_ASSERT_EQUAL_STRING("####----", buf);
    render_meter(buf, sizeof(buf), 0, 10, 8, false);
    TEST_ASSERT_EQUAL_STRING("--------", buf);
    render_meter(buf, sizeof(buf), 10, 10, 8, false);
    TEST_ASSERT_EQUAL_STRING("########", buf);
    /* Half a cell short of the fifth block shows the partial marker. */
    render_meter(buf, sizeof(buf), 9, 16, 8, false);
    TEST_ASSERT_EQUAL_STRING("####=---", buf);
    render_meter(buf, sizeof(buf), 3, 0, 8, false);
    TEST_ASSERT_EQUAL_STRING("--------", buf);

    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
        setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        TEST_IGNORE_MESSAGE("no UTF-8 locale on this machine");
    }
    /* The block glyphs are three bytes each, so the only width that means
       anything here is the column count. */
    render_meter(buf, sizeof(buf), 5, 10, 8, true);
    TEST_ASSERT_EQUAL_INT(8, render_display_width(buf));
    render_meter(buf, sizeof(buf), 3, 7, 12, true);
    TEST_ASSERT_EQUAL_INT(12, render_display_width(buf));
    setlocale(LC_CTYPE, "C");
}

/* The gate every box, rule and meter passes through. The piped case is the
   one that matters most: the e2e suite pins an 80x24 size while redirecting
   stdout, so FULL mode alone must never be enough to draw anything. */
static void test_decorate_needs_both_a_tty_and_the_room(void) {
    TEST_ASSERT_TRUE(render_decorate_for(true, RENDER_FULL));
    TEST_ASSERT_TRUE(render_decorate_for(true, RENDER_COMPACT));
    TEST_ASSERT_FALSE(render_decorate_for(false, RENDER_FULL));
    TEST_ASSERT_FALSE(render_decorate_for(true, RENDER_MINIMAL));
    TEST_ASSERT_FALSE(render_decorate_for(false, RENDER_MINIMAL));
}

/* Two distinct tables, both complete. A NULL member would print as a crash
   rather than as a missing corner. */
static void test_box_tables_are_complete_and_distinct(void) {
    const render_box_t *uni = render_box_for(true);
    const render_box_t *ascii = render_box_for(false);
    TEST_ASSERT_NOT_NULL(uni->tl); TEST_ASSERT_NOT_NULL(uni->tr);
    TEST_ASSERT_NOT_NULL(uni->bl); TEST_ASSERT_NOT_NULL(uni->br);
    TEST_ASSERT_NOT_NULL(uni->h);
    TEST_ASSERT_NOT_NULL(ascii->tl); TEST_ASSERT_NOT_NULL(ascii->tr);
    TEST_ASSERT_NOT_NULL(ascii->bl); TEST_ASSERT_NOT_NULL(ascii->br);
    TEST_ASSERT_NOT_NULL(ascii->h);
    TEST_ASSERT_EQUAL_STRING("+", ascii->tl);
    TEST_ASSERT_EQUAL_STRING("-", ascii->h);
    /* The ASCII table has to stay one byte per piece, since that branch runs
       where nothing multibyte can be decoded at all. */
    TEST_ASSERT_EQUAL_INT(1, (int)strlen(ascii->h));
    TEST_ASSERT_TRUE(strcmp(uni->tl, ascii->tl) != 0);
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
    RUN_TEST(test_render_escapes_follow_the_tty_alone);
    RUN_TEST(test_render_depth_reads_colorterm_and_term);
    RUN_TEST(test_render_style_follows_the_depth);
    RUN_TEST(test_rgb_to_256_hits_the_right_cube_and_ramp);
    RUN_TEST(test_label_colors_are_stable_and_never_dim);
    RUN_TEST(test_utf8_detection_reads_the_codeset);
    RUN_TEST(test_display_width_counts_columns_not_bytes);
    RUN_TEST(test_meter_fills_by_eighths);
    RUN_TEST(test_meter_draws_a_fixed_width_bar);
    RUN_TEST(test_decorate_needs_both_a_tty_and_the_room);
    RUN_TEST(test_box_tables_are_complete_and_distinct);
    RUN_TEST(test_wordmark_fits_an_eighty_column_terminal);
    RUN_TEST(test_compact_logo_fits_a_forty_column_terminal);
    return UNITY_END();
}

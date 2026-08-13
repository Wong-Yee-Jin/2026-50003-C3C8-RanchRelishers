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

/* One slot at all four depths, dark background. The empty string at the
   bottom is the case the e2e suite depends on: piped output must carry no
   escape bytes. */
static void test_render_style_follows_the_depth(void) {
    char buf[24];
    TEST_ASSERT_EQUAL_STRING("\x1b[38;2;94;200;240m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_TRUE, RENDER_BG_DARK));
    TEST_ASSERT_EQUAL_STRING("\x1b[38;5;117m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_256, RENDER_BG_DARK));
    TEST_ASSERT_EQUAL_STRING("\x1b[96m",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_16, RENDER_BG_DARK));
    TEST_ASSERT_EQUAL_STRING("",
        render_style_into(buf, sizeof(buf), RENDER_ACCENT, RENDER_COLOR_NONE, RENDER_BG_DARK));
    /* Every slot has to produce something at 16 colors, or a call site would
       silently lose its styling on a plain xterm. */
    for (int slot = 0; slot < RENDER_SLOT_COUNT; slot++) {
        render_style_into(buf, sizeof(buf), (render_slot_t)slot, RENDER_COLOR_16, RENDER_BG_DARK);
        TEST_ASSERT_TRUE(buf[0] == '\x1b');
    }
}

/* The light table has to be a genuinely different palette, not the dark one
   relabeled, or a light terminal gets the same washed-out colors this wave
   exists to fix. GitHub's light accent blue is the known answer to check
   against. */
static void test_render_style_swaps_palette_with_background(void) {
    char dark[24], light[24];
    render_style_into(dark, sizeof(dark), RENDER_ACCENT, RENDER_COLOR_TRUE, RENDER_BG_DARK);
    render_style_into(light, sizeof(light), RENDER_ACCENT, RENDER_COLOR_TRUE, RENDER_BG_LIGHT);
    TEST_ASSERT_EQUAL_STRING("\x1b[38;2;9;105;218m", light);
    TEST_ASSERT_TRUE(strcmp(dark, light) != 0);
    /* RENDER_COLOR_NONE has to stay silent regardless of which table it would
       have picked from, since "no color" cannot depend on a palette. */
    TEST_ASSERT_EQUAL_STRING("",
        render_style_into(light, sizeof(light), RENDER_ACCENT, RENDER_COLOR_NONE, RENDER_BG_LIGHT));
}

/* The rule table from the design doc: unset or unparsable is dark, 0-6 and 8
   are dark, 7 and 9-15 are light, and only the last ";"-separated field
   counts (some terminals report "fg;bg", others "fg;unused;bg"). */
static void test_render_background_reads_the_last_colorfgbg_field(void) {
    TEST_ASSERT_EQUAL_INT(RENDER_BG_DARK, render_background_for(NULL));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_DARK, render_background_for(""));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_DARK, render_background_for("not a number"));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_DARK, render_background_for("15;0"));      // bg is the LAST field
    TEST_ASSERT_EQUAL_INT(RENDER_BG_DARK, render_background_for("0;8"));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_LIGHT, render_background_for("0;15"));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_LIGHT, render_background_for("0;7"));
    TEST_ASSERT_EQUAL_INT(RENDER_BG_LIGHT, render_background_for("15;default;9"));  // three-field form
    for (int v = 0; v <= 15; v++) {
        char field[8];
        snprintf(field, sizeof(field), "0;%d", v);
        render_background_t want = (v == 7 || (v >= 9 && v <= 15)) ? RENDER_BG_LIGHT : RENDER_BG_DARK;
        TEST_ASSERT_EQUAL_INT(want, render_background_for(field));
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

/* The two ends and the midpoint of a lerp, which is the whole of what the
   splash sweep needs to trust: column 0 is exactly the first stop, the last
   column is exactly the second, and the middle sits between them. */
static void test_gradient_at_hits_endpoints_and_midpoint(void) {
    int r, g, b;
    render_gradient_at(0, 11, 10, 20, 30, 110, 120, 130, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(10, r); TEST_ASSERT_EQUAL_INT(20, g); TEST_ASSERT_EQUAL_INT(30, b);

    render_gradient_at(10, 11, 10, 20, 30, 110, 120, 130, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(110, r); TEST_ASSERT_EQUAL_INT(120, g); TEST_ASSERT_EQUAL_INT(130, b);

    render_gradient_at(5, 11, 10, 20, 30, 110, 120, 130, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(60, r); TEST_ASSERT_EQUAL_INT(70, g); TEST_ASSERT_EQUAL_INT(80, b);

    /* A column past either edge clamps to the matching endpoint rather than
       dividing by something outside the two stops. */
    render_gradient_at(-3, 11, 10, 20, 30, 110, 120, 130, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(10, r);
    render_gradient_at(99, 11, 10, 20, 30, 110, 120, 130, &r, &g, &b);
    TEST_ASSERT_EQUAL_INT(110, r);
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

/* Escapes are skipped rather than counted, so a caret or a label name wrapped
   in render_style() still measures as however many columns it actually
   draws. */
static void test_visible_width_skips_escape_sequences(void) {
    TEST_ASSERT_EQUAL_INT(0, render_visible_width(""));
    TEST_ASSERT_EQUAL_INT(5, render_visible_width("hello"));
    TEST_ASSERT_EQUAL_INT(5, render_visible_width("\x1b[96mhello\x1b[0m"));

    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
        setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        TEST_IGNORE_MESSAGE("no UTF-8 locale on this machine");
    }
    /* The caret glyph itself, the same "\xe2\x9d\xaf" ui_row wraps in an
       accent escape, still measures as one column once the color is
       skipped. */
    TEST_ASSERT_EQUAL_INT(2, render_visible_width("\x1b[38;2;9;105;218m\xe2\x9d\xafx\x1b[0m"));
    setlocale(LC_CTYPE, "C");
}

/* Plain ASCII, an exact fit that needs no cut, a width too small even for the
   ellipsis, and the case the dossier calls out by name: a CJK string cut
   before the wide character rather than through it. */
static void test_truncate_never_splits_a_wide_character(void) {
    char out[64];

    /* Pure ASCII in and out, so these hold regardless of locale. */
    render_truncate("hello world", 5, false, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("he...", out);              // 2 plain columns + 3-column ellipsis
    render_truncate("hello", 5, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello", out);              // exact fit, nothing cut
    render_truncate("hello", 10, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello", out);              // shorter than the budget
    /* Too narrow for even the ellipsis: a hard cut with no ellipsis at all,
       never wider than the requested budget. */
    render_truncate("hello", 2, false, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("he", out);
    render_truncate("hello", 0, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);

    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
        setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        TEST_IGNORE_MESSAGE("no UTF-8 locale on this machine");
    }
    render_truncate("hello world", 5, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hell\xe2\x80\xa6", out);   // 4 plain columns + 1-column ellipsis
    TEST_ASSERT_EQUAL_INT(5, render_display_width(out));

    /* Three two-column CJK characters (6 columns) cut to a 5-column budget
       must drop the whole third character rather than showing half of it. */
    const char *cjk = "\xe4\xb8\xad\xe6\x96\x87\xe5\xad\x97";   // "中文字"
    render_truncate(cjk, 5, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("\xe4\xb8\xad\xe6\x96\x87\xe2\x80\xa6", out);   // "中文…"
    TEST_ASSERT_TRUE(render_display_width(out) <= 5);
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
    TEST_ASSERT_NOT_NULL(uni->h);  TEST_ASSERT_NOT_NULL(uni->v);
    TEST_ASSERT_NOT_NULL(ascii->tl); TEST_ASSERT_NOT_NULL(ascii->tr);
    TEST_ASSERT_NOT_NULL(ascii->bl); TEST_ASSERT_NOT_NULL(ascii->br);
    TEST_ASSERT_NOT_NULL(ascii->h);  TEST_ASSERT_NOT_NULL(ascii->v);
    TEST_ASSERT_EQUAL_STRING("+", ascii->tl);
    TEST_ASSERT_EQUAL_STRING("-", ascii->h);
    TEST_ASSERT_EQUAL_STRING("|", ascii->v);
    /* The ASCII table has to stay one byte per piece, since that branch runs
       where nothing multibyte can be decoded at all. */
    TEST_ASSERT_EQUAL_INT(1, (int)strlen(ascii->h));
    TEST_ASSERT_EQUAL_INT(1, (int)strlen(ascii->v));
    TEST_ASSERT_TRUE(strcmp(uni->tl, ascii->tl) != 0);
    TEST_ASSERT_TRUE(strcmp(uni->v, ascii->v) != 0);
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

/* The spinner has to keep spinning for as long as a call takes, so index N
   and index N+(the frame count) must land on the same glyph, and the ASCII
   fallback has to be a genuinely different, shorter set rather than the
   UTF-8 one relabeled. */
static void test_spinner_frame_wraps_and_picks_ascii(void) {
    const char *first = render_spinner_frame(0, true);
    TEST_ASSERT_EQUAL_STRING(first, render_spinner_frame(10, true));   // 10 braille frames
    TEST_ASSERT_TRUE(strcmp(first, render_spinner_frame(1, true)) != 0);

    const char *ascii_first = render_spinner_frame(0, false);
    TEST_ASSERT_EQUAL_STRING(ascii_first, render_spinner_frame(4, false));   // 4 ASCII frames
    TEST_ASSERT_EQUAL_STRING("|", ascii_first);
    TEST_ASSERT_EQUAL_STRING("/", render_spinner_frame(1, false));

    /* Both sets round-trip through a negative index rather than reading out
       of bounds, since a caller's counter is never guaranteed non-negative. */
    TEST_ASSERT_EQUAL_STRING(ascii_first, render_spinner_frame(-1, false));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_mode_follows_terminal_size);
    RUN_TEST(test_render_color_honors_no_color_and_tty);
    RUN_TEST(test_render_escapes_follow_the_tty_alone);
    RUN_TEST(test_render_depth_reads_colorterm_and_term);
    RUN_TEST(test_render_style_follows_the_depth);
    RUN_TEST(test_render_style_swaps_palette_with_background);
    RUN_TEST(test_render_background_reads_the_last_colorfgbg_field);
    RUN_TEST(test_rgb_to_256_hits_the_right_cube_and_ramp);
    RUN_TEST(test_gradient_at_hits_endpoints_and_midpoint);
    RUN_TEST(test_label_colors_are_stable_and_never_dim);
    RUN_TEST(test_utf8_detection_reads_the_codeset);
    RUN_TEST(test_display_width_counts_columns_not_bytes);
    RUN_TEST(test_visible_width_skips_escape_sequences);
    RUN_TEST(test_truncate_never_splits_a_wide_character);
    RUN_TEST(test_meter_fills_by_eighths);
    RUN_TEST(test_meter_draws_a_fixed_width_bar);
    RUN_TEST(test_decorate_needs_both_a_tty_and_the_room);
    RUN_TEST(test_box_tables_are_complete_and_distinct);
    RUN_TEST(test_wordmark_fits_an_eighty_column_terminal);
    RUN_TEST(test_compact_logo_fits_a_forty_column_terminal);
    RUN_TEST(test_spinner_frame_wraps_and_picks_ascii);
    return UNITY_END();
}

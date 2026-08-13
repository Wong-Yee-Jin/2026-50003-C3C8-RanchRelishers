#include "unity.h"
#include "ui/input.h"
#include <string.h>

/* The decoder is pure: it reads a caller-owned byte buffer and reports what
   the first key in it was. That is the whole reason it exists as its own
   function, since none of these cases can be typed at a test binary. */
void setUp(void) {}
void tearDown(void) {}

/* Feeds a literal and checks both halves of the answer: which key it was and
   how many bytes it took. Getting the count wrong is the failure that hurts,
   because leftover bytes arrive later as keys nobody pressed. */
static void expect(const char *bytes, size_t len, key_type_t type, size_t used) {
    key_event_t ev;
    size_t n = input_decode(bytes, len, &ev);
    TEST_ASSERT_EQUAL_size_t(used, n);
    TEST_ASSERT_EQUAL_INT(type, ev.type);
}

/* The two arrow encodings a terminal picks between on its own: CSI in the
   normal mode, SS3 once an application has turned on cursor key mode. Both
   have to work, since which one arrives is not ours to decide. */
static void test_input_reads_csi_arrows(void) {
    expect("\x1b[A", 3, KEY_UP, 3);
    expect("\x1b[B", 3, KEY_DOWN, 3);
}

static void test_input_reads_ss3_arrows(void) {
    expect("\x1bOA", 3, KEY_UP, 3);
    expect("\x1bOB", 3, KEY_DOWN, 3);
}

/* Raw mode gives us \r, but a pty driven by a script sends \n, and a person
   pressing Enter means the same thing either way. */
static void test_input_reads_both_enter_bytes(void) {
    expect("\r", 1, KEY_ENTER, 1);
    expect("\n", 1, KEY_ENTER, 1);
}

/* A lone Esc is the first byte of every arrow key, so the decoder cannot
   answer yet and says so. Only the reader's timeout can tell the two apart. */
static void test_input_leaves_a_lone_esc_undecided(void) {
    expect("\x1b", 1, KEY_NONE, 0);
    expect("\x1b[", 2, KEY_NONE, 0);
}

static void test_input_reads_a_printable_character(void) {
    key_event_t ev;
    TEST_ASSERT_EQUAL_size_t(1, input_decode("c", 1, &ev));
    TEST_ASSERT_EQUAL_INT(KEY_CHAR, ev.type);
    TEST_ASSERT_EQUAL_INT('c', ev.ch);
}

/* Raw mode drops ISIG, so these two arrive as ordinary bytes and the menu is
   the only thing that can act on them. */
static void test_input_reads_ctrl_c_and_ctrl_d(void) {
    expect("\x03", 1, KEY_INTERRUPT, 1);
    expect("\x04", 1, KEY_EOF, 1);
}

/* An arrow key split across two reads: the first half decodes to nothing and
   consumes nothing, so the second half completes it instead of being read as
   a bracket and a letter someone typed. */
static void test_input_waits_for_a_split_sequence(void) {
    key_event_t ev;
    char buf[8];
    memcpy(buf, "\x1b[", 2);
    TEST_ASSERT_EQUAL_size_t(0, input_decode(buf, 2, &ev));
    TEST_ASSERT_EQUAL_INT(KEY_NONE, ev.type);

    buf[2] = 'B';
    TEST_ASSERT_EQUAL_size_t(3, input_decode(buf, 3, &ev));
    TEST_ASSERT_EQUAL_INT(KEY_DOWN, ev.type);
}

/* A sequence we have no use for still has to be swallowed whole. Left behind,
   the "1", ";" and "5" of a ctrl-arrow would arrive as three menu keystrokes,
   and "1" is a menu choice. */
static void test_input_swallows_sequences_it_does_not_use(void) {
    expect("\x1b[1;5A", 6, KEY_IGNORED, 6);
    expect("\x1b[5~", 4, KEY_IGNORED, 4);
    expect("\x1bOP", 3, KEY_IGNORED, 3);
    expect("\x1bx", 2, KEY_IGNORED, 2);
}

/* An unfinished sequence stays unfinished no matter how much of it is here,
   so a mouse report cut off mid-parameter never turns into keystrokes. */
static void test_input_holds_an_unterminated_sequence(void) {
    expect("\x1b[1;5", 5, KEY_NONE, 0);
}

/* Tab and the rest of the control range mean nothing on these screens, but
   they are consumed rather than left to be decoded again forever. */
static void test_input_consumes_control_bytes(void) {
    expect("\t", 1, KEY_IGNORED, 1);
    expect("\x7f", 1, KEY_IGNORED, 1);
}

static void test_input_reports_an_empty_buffer_as_incomplete(void) {
    expect("", 0, KEY_NONE, 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_input_reads_csi_arrows);
    RUN_TEST(test_input_reads_ss3_arrows);
    RUN_TEST(test_input_reads_both_enter_bytes);
    RUN_TEST(test_input_leaves_a_lone_esc_undecided);
    RUN_TEST(test_input_reads_a_printable_character);
    RUN_TEST(test_input_reads_ctrl_c_and_ctrl_d);
    RUN_TEST(test_input_waits_for_a_split_sequence);
    RUN_TEST(test_input_swallows_sequences_it_does_not_use);
    RUN_TEST(test_input_holds_an_unterminated_sequence);
    RUN_TEST(test_input_consumes_control_bytes);
    RUN_TEST(test_input_reports_an_empty_buffer_as_incomplete);
    return UNITY_END();
}

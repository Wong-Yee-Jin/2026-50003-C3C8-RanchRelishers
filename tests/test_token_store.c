#include "unity.h"
#include "token_store.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_CFG_DIR "/tmp/mght_test_cfg"

/* Point the store at a throwaway config dir and wipe it before every case, so
   a token one case saved can never make the next one look logged in. */
void setUp(void) {
    setenv("XDG_CONFIG_HOME", TEST_CFG_DIR, 1);
    (void)system("rm -rf " TEST_CFG_DIR);
}

void tearDown(void) {
    (void)system("rm -rf " TEST_CFG_DIR);
}

/* Use case: Sign Up / Log In. A missing file has to read as "never logged in"
   rather than as an empty token. */
static void test_log_in_finds_no_token_before_the_first_save(void) {
    char buf[256];
    TEST_ASSERT_FALSE(token_load(buf, sizeof(buf)));
}

/* Use case: Sign Up / Log In. Caching the token is what saves the person from
   repeating the device flow on every run. */
static void test_log_in_saves_and_reloads_the_token(void) {
    char buf[256];
    TEST_ASSERT_TRUE(token_save("gho_secret123"));
    TEST_ASSERT_TRUE(token_load(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("gho_secret123", buf);
}

/* The token is a bearer secret for the person's whole GitHub account, so the
   file it lands in must not be readable by anyone else on the machine. */
static void test_log_in_token_file_is_owner_only(void) {
    TEST_ASSERT_TRUE(token_save("gho_secret123"));
    char path[512];
    TEST_ASSERT_TRUE(token_path(path, sizeof(path)));
    struct stat stt;
    TEST_ASSERT_EQUAL_INT(0, stat(path, &stt));
    TEST_ASSERT_EQUAL_INT(0600, stt.st_mode & 0777);   // owner-only
}

/* Use case: Sign Up / Log In, in reverse. Logging out has to actually remove
   the cached token, not just forget it in memory. */
static void test_log_out_clears_the_saved_token(void) {
    char buf[256];
    TEST_ASSERT_TRUE(token_save("gho_secret123"));
    token_clear();
    TEST_ASSERT_FALSE(token_load(buf, sizeof(buf)));   // gone after clear
}

/* A 0-byte token file can show up if a save was interrupted before the old
   O_TRUNC write ever landed a byte. It must read the same as no file at all,
   not as a cached empty token. */
static void test_log_in_treats_an_empty_token_file_as_no_login(void) {
    char path[512];
    TEST_ASSERT_TRUE(token_path(path, sizeof(path)));
    TEST_ASSERT_TRUE(token_save("gho_secret123"));   // creates the parent dir
    int fd = open(path, O_WRONLY | O_TRUNC, 0600);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);

    char buf[256];
    TEST_ASSERT_FALSE(token_load(buf, sizeof(buf)));
}

/* A lone newline is what an editor or a stray `echo >` leaves behind. After
   the trailing-newline trim that is also an empty token, so it has to fail
   the same way a 0-byte file does. */
static void test_log_in_treats_a_bare_newline_file_as_no_login(void) {
    char path[512];
    TEST_ASSERT_TRUE(token_path(path, sizeof(path)));
    TEST_ASSERT_TRUE(token_save("gho_secret123"));   // creates the parent dir
    int fd = open(path, O_WRONLY | O_TRUNC, 0600);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(1, write(fd, "\n", 1));
    close(fd);

    char buf[256];
    TEST_ASSERT_FALSE(token_load(buf, sizeof(buf)));
}

/* Logging in again after the token already expired or changed has to
   actually replace the old file, and the atomic-rename path must still land
   the file at 0600 like the direct-write path used to. */
static void test_log_in_again_replaces_the_old_token(void) {
    char buf[256];
    TEST_ASSERT_TRUE(token_save("gho_old_token"));
    TEST_ASSERT_TRUE(token_save("gho_new_token"));
    TEST_ASSERT_TRUE(token_load(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("gho_new_token", buf);

    char path[512];
    TEST_ASSERT_TRUE(token_path(path, sizeof(path)));
    struct stat stt;
    TEST_ASSERT_EQUAL_INT(0, stat(path, &stt));
    TEST_ASSERT_EQUAL_INT(0600, stt.st_mode & 0777);
}

/* An empty string is not a token. Saving it must fail outright rather than
   land a 0-byte file that token_load would then also have to reject. */
static void test_log_in_rejects_saving_an_empty_token(void) {
    TEST_ASSERT_FALSE(token_save(""));
    char buf[256];
    TEST_ASSERT_FALSE(token_load(buf, sizeof(buf)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_in_finds_no_token_before_the_first_save);
    RUN_TEST(test_log_in_saves_and_reloads_the_token);
    RUN_TEST(test_log_in_token_file_is_owner_only);
    RUN_TEST(test_log_out_clears_the_saved_token);
    RUN_TEST(test_log_in_treats_an_empty_token_file_as_no_login);
    RUN_TEST(test_log_in_treats_a_bare_newline_file_as_no_login);
    RUN_TEST(test_log_in_again_replaces_the_old_token);
    RUN_TEST(test_log_in_rejects_saving_an_empty_token);
    return UNITY_END();
}

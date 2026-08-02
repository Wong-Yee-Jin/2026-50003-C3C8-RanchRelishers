#include "unity.h"
#include "token_store.h"
#include <stdlib.h>
#include <sys/stat.h>

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_in_finds_no_token_before_the_first_save);
    RUN_TEST(test_log_in_saves_and_reloads_the_token);
    RUN_TEST(test_log_in_token_file_is_owner_only);
    RUN_TEST(test_log_out_clears_the_saved_token);
    return UNITY_END();
}

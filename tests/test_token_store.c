#include "test.h"
#include "token_store.h"
#include <stdlib.h>
#include <sys/stat.h>

int main(void) {
    setenv("XDG_CONFIG_HOME", "/tmp/mght_test_cfg", 1);
    system("rm -rf /tmp/mght_test_cfg");
    char buf[256];
    CHECK(token_load(buf, sizeof(buf)) == false);   // nothing saved yet
    CHECK(token_save("gho_secret123") == true);
    CHECK(token_load(buf, sizeof(buf)) == true);
    CHECK_STR(buf, "gho_secret123");

    char path[512]; token_path(path, sizeof(path));
    struct stat stt; stat(path, &stt);
    CHECK((stt.st_mode & 0777) == 0600);            // owner-only

    token_clear();
    CHECK(token_load(buf, sizeof(buf)) == false);   // gone after clear
    system("rm -rf /tmp/mght_test_cfg");
    return TEST_SUMMARY();
}

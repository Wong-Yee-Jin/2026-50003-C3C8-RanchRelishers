#include "test.h"
#include "util.h"
#include <ctype.h>

int main(void) {
    char id[ID_LEN];
    CHECK(id_generate(id) == true);
    CHECK(strlen(id) == 24);           // 12 random bytes rendered as hex
    for (int i = 0; id[i]; i++) CHECK(isxdigit((unsigned char)id[i]));

    char id2[ID_LEN];
    id_generate(id2);
    CHECK(strcmp(id, id2) != 0);       // two calls do not collide in practice
    return TEST_SUMMARY();
}

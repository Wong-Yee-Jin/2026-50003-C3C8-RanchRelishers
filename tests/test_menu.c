#include "test.h"
#include "ui/menu.h"
int main(void) {
    char a[] = "  3\n"; CHECK(ui_parse_choice(ui_trim(a)) == 3);
    char b[] = "\n";    CHECK(ui_parse_choice(ui_trim(b)) == -1);
    char c[] = "x\n";   CHECK(ui_parse_choice(ui_trim(c)) == -1);
    char d[] = " hi \n"; CHECK_STR(ui_trim(d), "hi");
    return TEST_SUMMARY();
}

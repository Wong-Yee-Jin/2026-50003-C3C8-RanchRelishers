#include "test.h"
#include "render.h"
#include "assets.h"
#include <string.h>
int main(void) {
    CHECK(render_mode_for(80, 24) == RENDER_FULL);
    CHECK(render_mode_for(120, 40) == RENDER_FULL);
    CHECK(render_mode_for(79, 24) == RENDER_COMPACT);
    CHECK(render_mode_for(80, 23) == RENDER_COMPACT);
    CHECK(render_mode_for(40, 10) == RENDER_COMPACT);
    CHECK(render_mode_for(39, 24) == RENDER_MINIMAL);
    CHECK(render_color_for(true, NULL) == true);
    CHECK(render_color_for(true, "") == true);      // empty NO_COLOR means unset
    CHECK(render_color_for(true, "1") == false);
    CHECK(render_color_for(false, NULL) == false);  // not a tty, never color

    // Frame-geometry sanity: the art must never wrap or clip in an 80-col
    // terminal, and needs enough rows to read as block letters rather than
    // a single squished line.
    CHECK(wordmark_height() >= 5);
    for (int i = 0; wordmark[i]; i++) {
        CHECK((int)strlen(wordmark[i]) <= 80);
        CHECK((int)strlen(wordmark[i]) == wordmark_width());
    }
    CHECK((int)strlen(logo_compact) <= 40);

    return TEST_SUMMARY();
}

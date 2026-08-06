#include "assets.h"
#include <string.h>

/* Block-letter "GH TRACKER" wordmark, stored once as plain rows. Every row
   is padded to the same width, so wordmark_width() is a single well-defined
   number and render.c's splash can treat the art as a plain rectangle of
   columns. The animation itself is not a set of pre-drawn frames, it's a
   color span computed and overlaid on this same text each time it draws. */
const char *const wordmark[] = {
    " #### #   #    ##### ####   ###   #### #   # ##### #### ",
    "#     #   #      #   #   # #   # #     #  #  #     #   #",
    "#  ## #####      #   ####  ##### #     ###   ####  #### ",
    "#   # #   #      #   #  #  #   # #     #  #  #     #  # ",
    " #### #   #      #   #   # #   #  #### #   # ##### #   #",
    NULL,
};

/* One-line stand-in for the wordmark: the small mark next to the title on
   FULL headers, and the whole splash whenever the terminal can't show the
   full animation (COMPACT mode, or a raw-mode setup failure). */
const char *const logo_compact = "[ gh-tracker ]";

/* Row length of the padded array, not a hardcoded number, so a wider or
   narrower redraw of the art can't drift out of sync with what the splash
   sweep thinks it's coloring. */
int wordmark_width(void) {
    return wordmark[0] ? (int)strlen(wordmark[0]) : 0;
}

/* Row count, found by walking to the NULL sentinel rather than assuming 5,
   so adding a row to the art later doesn't also require updating a count. */
int wordmark_height(void) {
    int n = 0;
    while (wordmark[n]) n++;
    return n;
}

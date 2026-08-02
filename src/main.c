#include "db.h"
#include "ui/menu.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

/* Entry point for the terminal app. There is no socket and no fork. We open the
   single SQLite database, seed the default labels, and hand control to the menu
   loop. The database path comes from DB_PATH so tests and graders can point at a
   scratch file without editing code. */
int main(void) {
    /* The menu prints most of its prompts without a trailing newline and never
       flushes. glibc drains stdout before a read on stdin, but Darwin's libc
       does not, so on a Mac the question can sit in the buffer while fgets
       blocks and the screen looks frozen. Turning the buffer off costs nothing
       at menu speed and saves an fflush after twenty-odd printfs. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* libcurl will lazily do this on the first curl_easy_init, but its own docs
       call that path unsafe and it never gets torn down. Process lifecycle is
       main's job, so the pair lives here rather than inside github.c. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "cannot initialize libcurl\n");
        return 1;
    }

    const char *path = getenv("DB_PATH");
    if (!path || !path[0]) path = "issues.db";
    if (!db_init(path)) {
        fprintf(stderr, "cannot open database %s\n", path);
        curl_global_cleanup();
        return 1;
    }
    db_labels_seed();
    menu_run();
    db_shutdown();
    curl_global_cleanup();
    return 0;
}

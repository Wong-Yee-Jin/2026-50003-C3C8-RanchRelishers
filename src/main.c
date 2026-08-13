#include "db.h"
#include "render.h"
#include "ui/menu.h"
#include "dotenv.h"
#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

/* Entry point for the terminal app. There is no socket and no fork. We open the
   single SQLite database, seed the default labels, and hand control to the menu
   loop. The database path comes from DB_PATH so tests and graders can point at a
   scratch file without editing code.

   Standalone build: unlike the tetrish-integrated version, this one never
   checks for a live tetrisd -- it runs on its own, with no dependency on
   any other server being up. */
int main(void) {
    /* Load .env (or $ENV_FILE) first, before anything else calls getenv():
       fills in GH_CLIENT_ID, DB_PATH, etc. from a file so they don't need
       to be exported by hand. Real exported env vars still win over
       whatever the file says. */
    const char *env_file = getenv("ENV_FILE");
    dotenv_load(env_file && env_file[0] ? env_file : ".env");

    /* A C program starts in the "C" locale no matter what LANG says, and in
       that locale nothing multibyte decodes. Without this the box and meter
       glyphs would measure as three columns each and every rule would come
       out the wrong length, so the locale has to be picked up before anything
       draws. LC_CTYPE alone, since number and date formatting are not ours
       to change. */
    setlocale(LC_CTYPE, "");

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

    /* Everything above can still fail and print to stderr on the screen the
       user was already looking at. From here on the app owns the screen, and
       render_screen_enter arranges for it to be handed back on exit or on a
       signal. Piped runs get none of this. */
    render_screen_enter();
    menu_run();
    db_shutdown();
    curl_global_cleanup();
    return 0;
}

#include "db.h"
#include "ui/menu.h"
#include "dotenv.h"
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

    const char *path = getenv("DB_PATH");
    if (!path || !path[0]) path = "issues.db";
    if (!db_init(path)) { fprintf(stderr, "cannot open database %s\n", path); return 1; }
    db_labels_seed();
    menu_run();
    db_shutdown();
    return 0;
}

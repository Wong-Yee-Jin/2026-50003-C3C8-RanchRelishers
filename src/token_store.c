#include "token_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Base config dir: $XDG_CONFIG_HOME or $HOME/.config. Split out from
   config_dir so token_save can mkdir this level too, since a test (or a
   first-ever run with a bare XDG_CONFIG_HOME) may not have it yet. */
static bool config_home(char *out, size_t outlen) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return (size_t)snprintf(out, outlen, "%s", xdg) < outlen;
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    return (size_t)snprintf(out, outlen, "%s/.config", home) < outlen;
}

/* Config dir, one level above the token file, so token_save can mkdir it
   before opening the file underneath it. */
static bool config_dir(char *out, size_t outlen) {
    char home[400];
    if (!config_home(home, sizeof(home))) return false;
    return (size_t)snprintf(out, outlen, "%s/mini-gh-tracker", home) < outlen;
}

/* Assemble the full path to the token file from the config dir. */
bool token_path(char *out, size_t outlen) {
    char dir[400];
    if (!config_dir(dir, sizeof(dir))) return false;
    return (size_t)snprintf(out, outlen, "%s/token", dir) < outlen;
}

/* Write the token to disk, creating ~/.config and our app dir first if they do
   not exist yet. Directories are made 0700 and the file 0600 so no other user
   on the machine can read the bearer secret. */
bool token_save(const char *token) {
    if (!token) return false;
    char home[400];
    if (!config_home(home, sizeof(home))) return false;
    mkdir(home, 0700);   // best effort; the real ~/.config usually exists already, we only need it there for the next mkdir

    char dir[400];
    if (!config_dir(dir, sizeof(dir))) return false;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return false;

    char path[512];
    if (!token_path(path, sizeof(path))) return false;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t len = strlen(token);
    ssize_t written = write(fd, token, len);
    /* Re-assert 0600 in case the file already existed with looser
       permissions from before this module owned it. */
    bool chmod_ok = fchmod(fd, 0600) == 0;
    close(fd);
    return written == (ssize_t)len && chmod_ok;
}

/* Read the cached token back into out. Returns false when the file does not
   exist yet, which the caller reads as "never logged in". */
bool token_load(char *out, size_t outlen) {
    if (!out || outlen == 0) return false;
    char path[512];
    if (!token_path(path, sizeof(path))) return false;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, outlen - 1);
    close(fd);
    if (n < 0) return false;

    if (n > 0 && out[n - 1] == '\n') n--;   // trailing newline from an editor or echo, not part of the token
    out[n] = '\0';
    return true;
}

/* Remove the token file, dropping the cached login so the next run starts
   unauthenticated. */
void token_clear(void) {
    char path[512];
    if (!token_path(path, sizeof(path))) return;
    unlink(path);   // ENOENT means already logged out, nothing to report
}

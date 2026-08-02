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
   on the machine can read the bearer secret.

   Written via a temp file plus rename rather than truncating the real path in
   place: a short write from a full disk or a signal used to leave a corrupt
   partial token sitting where token_load would trust it. rename() replaces
   the old file atomically, so a reader always sees either the old token or
   the new one, never a half-written one. No fsync before the rename, this is
   a cache we can regenerate with the device flow, not a record we need to
   survive a power loss for. */
bool token_save(const char *token) {
    if (!token || !*token) return false;   // an empty token is not a login, would just make token_load disagree
    char home[400];
    if (!config_home(home, sizeof(home))) return false;
    mkdir(home, 0700);   // best effort; the real ~/.config usually exists already, we only need it there for the next mkdir

    char dir[400];
    if (!config_dir(dir, sizeof(dir))) return false;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return false;

    char path[512];
    if (!token_path(path, sizeof(path))) return false;
    char tmp_path[520];
    if ((size_t)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= sizeof(tmp_path)) return false;

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0 && errno == EEXIST) {
        /* Stale temp left by a previous save that crashed before the rename.
           Clear it and retry once, rather than unlinking up front, so a
           concurrent saver's in-flight temp can't be deleted out from under
           it just because it happened to exist. */
        unlink(tmp_path);
        fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    }
    if (fd < 0) return false;

    size_t len = strlen(token);
    size_t off = 0;
    bool write_ok = true;
    while (off < len) {
        ssize_t written = write(fd, token + off, len - off);
        if (written < 0) {
            if (errno == EINTR) continue;   // interrupted by a signal, not a real failure, try again
            write_ok = false;
            break;
        }
        off += (size_t)written;
    }

    /* Put 0600 back in case a restrictive umask (say 0277) stripped the
       owner bits from the open() mode above; left alone, token_load's later
       read-only open would fail EACCES on the very file we just wrote. Not
       fatal: a normal umask already leaves the file at 0600, and on the odd
       filesystem where fchmod itself is refused we would rather keep the
       token we already wrote than throw it away. */
    fchmod(fd, 0600);
    close(fd);

    if (!write_ok) {
        unlink(tmp_path);
        return false;
    }
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return false;
    }
    return true;
}

/* Read the cached token back into out. Returns false when the file does not
   exist, or when it exists but has no token in it (a leftover empty or
   newline-only file), both of which the caller reads as "never logged in". */
bool token_load(char *out, size_t outlen) {
    if (!out || outlen == 0) return false;
    char path[512];
    if (!token_path(path, sizeof(path))) return false;

    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, outlen - 1);
    close(fd);
    if (n < 0) return false;

    if (n > 0 && out[n - 1] == '\n') n--;   // trailing newline from an editor or echo, not part of the token
    out[n] = '\0';
    return n > 0;
}

/* Remove the token file, dropping the cached login so the next run starts
   unauthenticated. */
void token_clear(void) {
    char path[512];
    if (!token_path(path, sizeof(path))) return;
    unlink(path);   // ENOENT means already logged out, nothing to report
}

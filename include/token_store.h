#ifndef TOKEN_STORE_H
#define TOKEN_STORE_H
#include <stdbool.h>
#include <stddef.h>

/* Caches the GitHub access token in a dotfile so the user does not repeat the
   device flow on every run. The token is a bearer secret for the user's
   GitHub account, so the file is created 0600 and the parent directory 0700,
   and re-asserted 0600 on every save in case something else loosened it.
   XDG_CONFIG_HOME is honored when set and non-empty, matching how the rest of
   a user's config tools resolve their own dotfiles; otherwise we fall back to
   HOME/.config, the XDG default. */

/* Write path into out as "<config dir>/mini-gh-tracker/token". Returns false
   when neither XDG_CONFIG_HOME nor HOME is usable, or out is too small. */
bool token_path(char *out, size_t outlen);

/* Store token at 0600, creating the parent directory (0700) first. Returns
   false if the directory or file cannot be created or the write is short, so
   a caller never mistakes a failed save for a cached login. */
bool token_save(const char *token);

/* Load the saved token into out, stripping one trailing newline. Returns
   false when no token file exists, distinguishing "never logged in" from an
   empty token. */
bool token_load(char *out, size_t outlen);

/* Remove the token file, e.g. on logout. A missing file is not an error. */
void token_clear(void);

#endif

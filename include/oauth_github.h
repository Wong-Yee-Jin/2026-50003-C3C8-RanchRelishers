#ifndef OAUTH_GITHUB_H
#define OAUTH_GITHUB_H

#include <stdbool.h>

#define GH_LOGIN_LEN  128
#define GH_NAME_LEN   128
#define GH_AVATAR_LEN 256
#define GH_TOKEN_LEN  256

typedef struct {
    long long id;
    char login[GH_LOGIN_LEN];
    char name[GH_NAME_LEN];
    char avatar_url[GH_AVATAR_LEN];
} gh_user_t;

/* Builds the github.com/login/oauth/authorize URL to redirect the browser to, embedding a CSRF `state` value the caller generated.
 * Returns false if GITHUB_CLIENT_ID isn't configured. */
bool oauth_github_authorize_url(const char *state, char *out, int outlen);

/* Exchanges a one-time `code` from the OAuth callback for an access token. 
 * Returns false on any failure (network, bad code, GitHub error, misconfiguration, etc). */
bool oauth_github_exchange_code(const char *code, char *token_out, int token_outlen);

/* Fetches the signed-in GitHub user's public profile using the access token from oauth_github_exchange_code(). */
bool oauth_github_fetch_user(const char *access_token, gh_user_t *out);

/* Checks whether `username` is a real GitHub account, the same way GitHub
 * itself validates a login when a repo owner adds a collaborator by
 * username. Calls the public, unauthenticated GET /users/:username
 * endpoint and returns true only on a 200 (account exists). Returns
 * false for malformed input, a 404, or any network/TLS failure -- callers
 * should treat all of those as "not a valid GitHub username". */
bool oauth_github_username_exists(const char *username);

#define GH_REPO_FULLNAME_LEN 140

/* Lists the repositories `access_token`'s account is an owner of or a
 * collaborator/contributor on (GitHub's /user/repos with
 * affiliation=owner,collaborator), as "owner/repo" full names. Only
 * repos visible under the token's granted scope are returned (public
 * repos, given this app's read:user scope). Returns the number of
 * repos written into `out` (capped at `max`), or -1 on failure. */
int oauth_github_fetch_repos(const char *access_token, char out[][GH_REPO_FULLNAME_LEN], int max);

/* Searches GitHub for accounts whose login matches `query` -- the same
 * autocomplete GitHub itself shows when a repo owner starts typing a
 * name into the "Add people" box. Uses the public Search Users API (no
 * token needed). Writes up to `max` matches into logins[]/avatars[]
 * (index i in one corresponds to index i in the other) and returns how
 * many were found; returns 0 for an empty query or on any failure. */
int oauth_github_search_users(const char *query, char logins[][GH_LOGIN_LEN], char avatars[][GH_AVATAR_LEN], int max);

#endif

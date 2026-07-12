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

#endif

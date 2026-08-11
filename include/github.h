#ifndef GITHUB_H
#define GITHUB_H
#include <stdbool.h>
#include <stddef.h>
#include "models.h"

/* Drives GitHub's OAuth device flow over HTTPS with libcurl. The device flow
   suits a terminal app because it has no redirect URL to catch: we ask GitHub
   for a code, the user authorizes it in a browser, and we poll for the token.
   Credentials come from the environment (GH_CLIENT_ID, GH_SCOPE) so no client
   id or token is ever compiled in. This module never prints or logs the token
   and stays UI-agnostic: it returns data and status, the caller displays it. */

/* The device_code identifies our pending request when polling; the user_code
   and verification_uri are what the person types into the browser. interval is
   the seconds GitHub asks us to wait between polls. */
typedef struct {
    char device_code[64];
    char user_code[32];
    char verification_uri[128];
    int interval;
} gh_device_t;

/* GH_PENDING/GH_SLOW_DOWN/GH_EXPIRED/GH_DENIED mirror the error strings GitHub
   returns while a device authorization is still in progress or has failed, so
   a polling caller can react without parsing strings itself. */
typedef enum { GH_OK, GH_PENDING, GH_SLOW_DOWN, GH_DENIED, GH_EXPIRED, GH_ERROR } gh_status_t;

/* Classify a token-endpoint response body without any network. An access_token
   field yields GH_OK and copies the token into token_out; otherwise the error
   field is mapped to the matching status, and anything unrecognized is
   GH_ERROR. Split out so the mapping is unit tested against fixed JSON. */
gh_status_t github_parse_token_response(const char *body, char *token_out, size_t outlen);

/* POST to the device-code endpoint and fill out from the response. Returns
   GH_ERROR when GH_CLIENT_ID is unset or the request or parse fails. */
gh_status_t github_device_start(gh_device_t *out);

/* One poll of the access-token endpoint for a device_code. Returns the parsed
   status, so the caller loops on GH_PENDING/GH_SLOW_DOWN until GH_OK or a
   terminal error. */
gh_status_t github_device_poll(const char *device_code, char *token_out, size_t outlen);

/* GET /user with the bearer token and upsert the result into SQLite via
   db_user_upsert_github. Reads id, login, name, and avatar_url from the
   response. GitHub allows a null name, so a missing or null name falls back
   to the login for the display name. Returns false on an HTTP failure, a
   missing id or login, or a failed upsert. rejected may be NULL; when given
   it is set true only when GitHub itself answered 401 or 403, meaning the
   token is actually invalid rather than the request never reaching GitHub,
   so a caller can tell a real rejection apart from a network problem. */
bool github_fetch_and_upsert_user(const char *token, user_t *out, bool *rejected);

/* Pull the name field out of each object in a top-level JSON array, such as
   the body of GET /user/repos. Pure and network-free so it is unit tested
   against a captured response. Returns how many names were copied. */
int github_parse_repo_names(const char *body, char names[][128], int max);

/* GET /user/repos with the bearer token and parse the repo names out of the
   response. Returns -1 on an HTTP failure so the caller can tell a failed
   request apart from an account with zero repos. */
int github_list_repos(const char *token, char names[][128], int max);

/* GET /search/users?q=... (unauthenticated -- no token needed or used) and
   copy up to max matching logins into out. Backs the "suggest real GitHub
   usernames as you add a contributor" flow: called once the person has
   typed a query and pressed enter, mirroring the web app's autocomplete
   but without needing a live keystroke-by-keystroke connection. Returns -1
   on an HTTP failure so the caller can tell "GitHub unreachable" apart
   from "zero accounts matched", and 0 for an empty query. */
int github_search_usernames(const char *query, char logins[][128], int max);
#endif

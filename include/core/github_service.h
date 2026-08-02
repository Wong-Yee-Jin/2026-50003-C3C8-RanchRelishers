#ifndef GITHUB_SERVICE_H
#define GITHUB_SERVICE_H

#include <stdbool.h>

/* Owns who the current session is signed in as. Two identities exist: a real
   GitHub account reached through the device flow, and the local user this app
   falls back to so it stays useful with no network and no account. Both are
   real rows in the users table, so auth_ctx always holds an id something can
   be attributed to.

   The GitHub half of that lives here rather than in the menu because it is a
   state machine, not a screen: request a code, wait for the person to approve
   it, persist the token, mirror the profile. Nothing below prints. Results
   come back as codes and strings for the caller to word however it likes. */

/* GH_SVC_LOCAL is not a failure. It means the session is running as the local
   user, either because there was no saved token or because the person logged
   out; from github_service_repos it means the same thing seen from the other
   side, no token to make the request with. GH_SVC_NO_PROFILE is the awkward
   middle: GitHub gave us a token but we could not read the account behind it,
   so the login half worked and the identity half did not. */
typedef enum {
    GH_SVC_OK,
    GH_SVC_LOCAL,
    GH_SVC_NO_PROFILE,
    GH_SVC_UNAVAILABLE,   /* the device flow would not open at all */
    GH_SVC_DENIED,        /* the person refused the authorization */
    GH_SVC_EXPIRED,       /* the code ran out before it was entered */
    GH_SVC_TIMED_OUT,     /* we stopped waiting first */
    GH_SVC_UNREACHABLE    /* GitHub kept failing to answer */
} gh_svc_result_t;

/* What the person has to be shown to authorize a login: the page to open and
   the code to type into it. Both point at storage this service owns and holds
   until the next github_service_login_start, so print them before starting
   another login rather than holding onto them. */
typedef struct {
    const char *verification_uri;
    const char *user_code;
} gh_login_prompt_t;

/* Restore the identity the last run left behind, for startup. A cached token
   that still checks out with GitHub gives GH_SVC_OK and the username below.
   A token GitHub rejects outright is dropped, since retrying it will not start
   working; one that fails on transport is kept, because a stalled connection
   says nothing about whether the token is still good. Either way the session
   ends up signed in, so a return of GH_SVC_LOCAL needs no handling beyond
   deciding whether to mention it. */
gh_svc_result_t github_service_resume(void);

/* Open a device authorization and hand back what to show the person. On
   GH_SVC_OK, display out and then call github_service_login_wait. */
gh_svc_result_t github_service_login_start(gh_login_prompt_t *out);

/* Wait out the authorization started above, polling GitHub on its own
   schedule. This blocks for as long as the person takes, up to a cap of a
   quarter hour or so, which suits a terminal menu with nothing else to do
   meanwhile. Only OK, NO_PROFILE, DENIED, EXPIRED, TIMED_OUT and UNREACHABLE
   come back from here.

   token_saved may be NULL. It is set false only when a token did arrive but
   could not be written to disk, and stays true everywhere else, so a caller
   can warn on false without first checking which result it got: the login
   worked, it just will not survive to the next run. */
gh_svc_result_t github_service_login_wait(bool *token_saved);

/* Drop the saved token and return the session to the local identity. */
void github_service_logout(void);

/* The GitHub username of the current session, or "" when it is local only.
   This is what tells a caller whether to offer logging in or logging out. */
const char *github_service_username(void);

/* Fill names with the account's repositories, at most max of them, and set
   *count to how many landed. GH_SVC_LOCAL means there is no token to ask
   with, GH_SVC_UNREACHABLE that the request itself failed, which is a
   different thing from an account with no repositories at all. */
gh_svc_result_t github_service_repos(char names[][128], int max, int *count);

#endif

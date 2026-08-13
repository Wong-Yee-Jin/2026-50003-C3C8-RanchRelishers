#include "core/github_service.h"
#include "core/auth_ctx.h"
#include "db.h"
#include "github.h"
#include "token_store.h"
#include <stdio.h>
#include <time.h>

/* The offline identity. It is an ordinary row in users like any assignee, so
   the id in auth_ctx always resolves to somebody, and the name is fixed so a
   second run finds the same row instead of piling up duplicates. */
#define LOCAL_USERNAME "local"

/* One process, one session, same as auth_ctx: statics are enough. gh_username
   is empty while the session is local, which is how the menu knows whether to
   offer a login or a logout. */
static char GH_USERNAME[USERNAME_LEN] = "";

/* The authorization login_start opened, kept here so the menu never has to
   carry the device code around between printing the prompt and waiting. */
static gh_device_t PENDING;

/* A poll should not wait forever on a code nobody ever entered. This caps it
   around GitHub's own 15 minute expiry at a 5 second interval, with room to
   spare for the slow_down backoff. */
#define GH_MAX_POLLS 180

/* Set only by github_service_set_tick, read only by poll_wait and the two
   calls into github.c below. NULL means nobody is watching, which is every
   piped run and every interactive one until the menu registers a spinner. */
static void (*GH_TICK)(void *ctx) = NULL;
static void *GH_TICK_CTX = NULL;

void github_service_set_tick(void (*tick)(void *ctx), void *ctx) {
    GH_TICK = tick;
    GH_TICK_CTX = ctx;
}

/* Sleeps for interval_secs in ~80ms slices, ticking the registered callback
   between them. Most of login_wait's runtime is spent right here, waiting
   out the interval GitHub asked for between polls, so this is where a UI
   spinner needs to keep animating rather than freezing for however many
   seconds interval_secs is. With no tick registered this is exactly
   interval_secs slept in smaller pieces, which costs nothing observable. */
static void poll_wait(int interval_secs) {
    long remaining_ms = (long)interval_secs * 1000;
    while (remaining_ms > 0) {
        long slice = remaining_ms < 80 ? remaining_ms : 80;
        struct timespec ts = { .tv_sec = slice / 1000, .tv_nsec = (slice % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        remaining_ms -= slice;
        if (GH_TICK) GH_TICK(GH_TICK_CTX);
    }
}

/* Point the session at the local user, creating the row the first time. The
   lookup passes over a GitHub account that happens to be named "local", so an
   offline session can never end up signed in as a real person.

   The row is only created on a definite miss, never when the lookup itself
   failed, or a database that errors on the read would get a fresh local user
   inserted next to the one it could not see. When neither branch produces a
   row we still sign in, on an id with nothing behind it. It reads wrong, and
   it is still the better of two bad options: the alternative is a session that
   is not signed in at all, where every screen answers "sign in first" and the
   person is left guessing. A database this broken will fail the next write
   anyway, and that failure at least says "database error". */
static void use_local_identity(void) {
    GH_USERNAME[0] = '\0';
    user_t local;
    int found = db_user_find_local(LOCAL_USERNAME, &local);
    if (found == 0 && db_user_create(LOCAL_USERNAME, &local)) found = 1;
    if (found == 1) {
        auth_ctx_set_user(local.id);
        return;
    }
    auth_ctx_set_user(LOCAL_USERNAME);
}

/* Turn a working token into the signed-in identity: read the profile from
   GitHub, mirror it into users so the account can be assigned to issues like
   any other, and point auth_ctx at that row. rejected is passed through from
   the fetch, so the caller can tell a refused token from an unreachable one. */
static bool adopt_github_user(const char *token, bool *rejected) {
    gh_profile_t profile;
    if (!github_fetch_user(token, &profile, rejected, GH_TICK, GH_TICK_CTX)) return false;

    user_t user;
    if (!db_user_upsert_github(profile.id, profile.login, profile.display_name,
                               profile.avatar_url, &user))
        return false;

    auth_ctx_set_user(user.id);
    snprintf(GH_USERNAME, sizeof(GH_USERNAME), "%s", user.username);
    return true;
}

gh_svc_result_t github_service_resume(void) {
    char token[256];
    if (token_load(token, sizeof(token))) {
        bool rejected = false;
        if (adopt_github_user(token, &rejected)) return GH_SVC_OK;
        if (rejected) token_clear();
    }
    use_local_identity();
    return GH_SVC_LOCAL;
}

gh_svc_result_t github_service_login_start(gh_login_prompt_t *out) {
    if (github_device_start(&PENDING) != GH_OK) {
        /* A start that failed partway can still have written some of PENDING,
           so clear the code the wait keys off. Left alone, a second attempt
           that failed early would go on polling the first attempt's code. */
        PENDING.device_code[0] = '\0';
        return GH_SVC_UNAVAILABLE;
    }
    out->verification_uri = PENDING.verification_uri;
    out->user_code = PENDING.user_code;
    return GH_SVC_OK;
}

gh_svc_result_t github_service_login_wait(bool *token_saved) {
    if (token_saved) *token_saved = true;
    // no code means no authorization is open, so there is nothing to wait on
    if (PENDING.device_code[0] == '\0') return GH_SVC_UNAVAILABLE;

    int interval = PENDING.interval;
    // a malformed device response could hand back an interval that busy-polls
    // or, passed straight to poll_wait, waits for close to forever
    if (interval < 1 || interval > 60) interval = 5;

    char token[256];
    int consecutive_errors = 0;
    for (int poll = 0; poll < GH_MAX_POLLS; poll++) {
        poll_wait(interval);
        gh_status_t status = github_device_poll(PENDING.device_code, token, sizeof(token));

        if (status == GH_OK) {
            /* Saved before the profile fetch: the token is the part worth
               keeping, and a network hiccup reading /user should not cost the
               person the login they just approved. */
            if (!token_save(token) && token_saved) *token_saved = false;
            /* No use for the rejection flag on a token GitHub minted seconds
               ago: there is no cached token to throw away either way. */
            return adopt_github_user(token, NULL) ? GH_SVC_OK : GH_SVC_NO_PROFILE;
        }
        if (status == GH_SLOW_DOWN) { interval += 5; consecutive_errors = 0; continue; }
        if (status == GH_DENIED) return GH_SVC_DENIED;
        if (status == GH_EXPIRED) return GH_SVC_EXPIRED;
        if (status == GH_ERROR) {
            // a momentary network blip should not throw away an in-progress
            // login; only give up after a few in a row
            if (++consecutive_errors >= 3) return GH_SVC_UNREACHABLE;
            continue;
        }
        consecutive_errors = 0;
        // GH_PENDING: the user has not authorized yet, poll again
    }
    return GH_SVC_TIMED_OUT;
}

void github_service_logout(void) {
    token_clear();
    use_local_identity();
}

const char *github_service_username(void) { return GH_USERNAME; }

gh_svc_result_t github_service_repos(char names[][128], int max, int *count) {
    *count = 0;
    /* Read from the file rather than a token held in memory, so this works on
       the first run after a resume as well as right after a fresh login. */
    char token[256];
    if (!token_load(token, sizeof(token))) return GH_SVC_LOCAL;

    int n = github_list_repos(token, names, max, GH_TICK, GH_TICK_CTX);
    if (n < 0) return GH_SVC_UNREACHABLE;
    *count = n;
    return GH_SVC_OK;
}

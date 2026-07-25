#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <openssl/rand.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "auth.h"
#include "oauth_github.h"

/*
 * auth_handlers.c
 * ---------------
 * "Register by linking GitHub" and "log in with GitHub" are the same
 * flow: GitHub OAuth doesn't distinguish first-time sign-in from a
 * returning one, so neither does this app -- db_user_upsert_github()
 * (src/db.c) creates the account the first time a given GitHub user id
 * is seen and just refreshes it every time after. See auth.c for the
 * first-party session this app keeps once that succeeds, and
 * oauth_github.c for the GitHub-facing HTTP calls.
 *
 *   GET  /login                  "Continue with GitHub" landing page
 *   GET  /auth/github            starts the OAuth redirect dance
 *   GET  /auth/github/callback   GitHub redirects back here with a code
 *   POST /logout                 ends our session only (not GitHub's)
 *
 * ADDITIONAL FEATURE: on every sign-in (registration or a returning
 * login), the callback pulls the account's current repo list from
 * GitHub -- public and private -- and creates a project for any repo
 * that doesn't already have one here yet, so newly-created or newly-
 * accessible GitHub repos keep showing up over time instead of only
 * at registration. See auto_import_github_projects() below.
 */

#define OAUTH_STATE_COOKIE "oauth_state"
#define OAUTH_STATE_LEN    64
#define GH_MAX_AUTO_REPOS  100

/* ---- ADDITIONAL FEATURE: auto-add projects from GitHub on every login ----
 * Every time a GitHub account signs in here -- first time or a
 * returning login -- pull the repos that account currently owns or is
 * a collaborator/contributor on (public and private) and create a
 * project for any of them that don't already exist in this app (named
 * "owner/repo"), so repos created or granted access to since the last
 * login show up without the person having to add them by hand.
 * Repos that already have a matching project here are silently
 * skipped via db_project_create()'s existing per-owner duplicate
 * check (db_project_name_exists()), which is also what keeps this
 * idempotent across repeated logins and guarantees no duplicate
 * project names. A failure to reach GitHub here never blocks the
 * login itself -- worst case nothing new gets added this time. */
static void auto_import_github_projects(const char *access_token, const char *owner_id) {
    char repos[GH_MAX_AUTO_REPOS][GH_REPO_FULLNAME_LEN];
    int n = oauth_github_fetch_repos(access_token, repos, GH_MAX_AUTO_REPOS);
    if (n < 0) {
        fprintf(stderr, "[auth] couldn't list GitHub repos to auto-add projects\n");
        return;
    }

    int created = 0;
    project_t tmp;
    for (int i = 0; i < n; i++) {
        if (db_project_create(owner_id, repos[i], &tmp)) created++;
    }
    if (created > 0)
        printf("[auth] auto-added %d new project(s) from GitHub on login\n", created);
}

static void gen_state(char out[OAUTH_STATE_LEN + 1]) {
    unsigned char raw[OAUTH_STATE_LEN / 2];
    RAND_bytes(raw, sizeof(raw));
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2]     = hex[(raw[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[raw[i] & 0xF];
    }
    out[OAUTH_STATE_LEN] = '\0';
}

/* ---- GET / ----
 * If nobody has signed in yet (no session / GitHub account linked), show landing page. 
*/
static void handle_home(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req; (void)params;
    user_t u;

    if (!auth_get_current_user(&u)) {
        char *page = render_landing_page();
        http_response_html(resp, 200, page);
        free(page);
        return;
    }

    http_response_redirect(resp, "/projects");
}

/* ---- GET /login ---- */
static void handle_login_page(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req; (void)params;
    user_t u;

    if (auth_get_current_user(&u)) {
        http_response_redirect(resp, "/projects");
        return;
    }

    sb_t sb; sb_init(&sb);
    sb_append(&sb,
        "<h1>Log in</h1>"
        "<p>Sign in -- or create an account, if this is your first time -- by "
        "linking your GitHub account. We use this to read your profile and the "
        "list of repos you own or collaborate on (including private ones), so "
        "we can keep your projects list in sync with GitHub; nothing is ever "
        "posted or changed on GitHub, and logging out here never signs you out "
        "of GitHub itself.</p>"
        "<p><a href='/auth/github'><button type='button'>Continue with GitHub</button></a></p>");
    char *page = render_page("Log in", sb.data);
    http_response_html(resp, 200, page);
    free(page); sb_free(&sb);
}

/* ---- GET /auth/github ---- */
static void handle_github_start(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req; (void)params;
    char state[OAUTH_STATE_LEN + 1];
    gen_state(state);
    char url[600];

    if (!oauth_github_authorize_url(state, url, sizeof(url))) {
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>GitHub login isn't configured</h1>"
                        "<p>The server is missing GITHUB_CLIENT_ID / "
                        "GITHUB_CLIENT_SECRET. See the README.</p>");
        char *page = render_page("Log in", sb.data);
        http_response_html(resp, 500, page);
        free(page); sb_free(&sb);
        return;
    }

    http_response_redirect(resp, url);

    /* Short-lived CSRF token for the callback to check against -- not
     * a login session, just proves this browser is the one we sent to
     * GitHub. */
    char cookie[192];
    snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=%s; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=600", OAUTH_STATE_COOKIE, state);
    http_response_add_header(resp, cookie);
}

/* ---- GET /auth/github/callback ---- */
static void handle_github_callback(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;
    char state[96] = {0}, cookie_state[96] = {0}, code[256] = {0};
    htttp_form_get(req->query, "state", state, sizeof(state));
    htttp_form_get(req->query, "code", code, sizeof(code));
    htttp_get_cookie(req, OAUTH_STATE_COOKIE, cookie_state, sizeof(cookie_state));

    /* CSRF check: the state we get back must match the one we minted
     * and stashed in a cookie right before sending the browser off to
     * GitHub. */
    if (!state[0] || !cookie_state[0] || strcmp(state, cookie_state) != 0) {
        char *page = render_page("Log in",
            "<h1>Login failed</h1><p>We couldn't verify this login attempt "
            "(state mismatch). <a href='/login'>Try again</a>.</p>");
        http_response_html(resp, 400, page);
        free(page);
        return;
    }

    if (!code[0]) {
        /* Alternative Flow: person declined the GitHub authorization prompt */
        char *page = render_page("Log in",
            "<h1>Login cancelled</h1><p>GitHub authorization wasn't granted. "
            "<a href='/login'>Try again</a>.</p>");
        http_response_html(resp, 400, page);
        free(page);
        return;
    }

    char access_token[GH_TOKEN_LEN];
    if (!oauth_github_exchange_code(code, access_token, sizeof(access_token))) {
        char *page = render_page("Log in",
            "<h1>Login failed</h1><p>We couldn't reach GitHub to complete "
            "sign-in. <a href='/login'>Try again</a>.</p>");
        http_response_html(resp, 502, page);
        free(page);
        return;
    }

    gh_user_t gh;
    if (!oauth_github_fetch_user(access_token, &gh)) {
        char *page = render_page("Log in",
            "<h1>Login failed</h1><p>We couldn't fetch your GitHub profile. "
            "<a href='/login'>Try again</a>.</p>");
        http_response_html(resp, 502, page);
        free(page);
        return;
    }

    user_t u;
    if (!db_user_upsert_github(gh.id, gh.login, gh.name, gh.avatar_url, &u)) {
        char *page = render_page("Log in",
            "<h1>Login failed</h1><p>We couldn't save your account. "
            "<a href='/login'>Try again</a>.</p>");
        http_response_html(resp, 500, page);
        free(page);
        return;
    }

    /* Every login (registration or a returning sign-in) re-syncs
     * projects against the account's current GitHub repos, so repos
     * created or newly shared with this account after their first
     * login still show up. Best-effort: never blocks login if GitHub
     * can't be reached or the repo list can't be fetched. See
     * auto_import_github_projects(). */
    auto_import_github_projects(access_token, u.id);

    http_response_redirect(resp, "/projects");
    /* one-time state cookie is spent -- clear it, then start the real session */
    http_response_add_header(resp, "Set-Cookie: " OAUTH_STATE_COOKIE "=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0");
    auth_start_session(resp, u.id);
}

/* ---- POST /logout ---- */
static void handle_logout(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;
    http_response_redirect(resp, "/login");
    auth_end_session(req, resp);
}

void auth_handlers_register(void) {
    router_add("GET", "/", handle_home);
    router_add("GET", "/login", handle_login_page);
    router_add("GET", "/auth/github", handle_github_start);
    router_add("GET", "/auth/github/callback", handle_github_callback);
    router_add("POST", "/logout", handle_logout);
}

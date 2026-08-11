#include "github.h"
#include "json.h"
#include "db.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* A bounded sink for a response body. cap is one less than the real buffer so
   there is always room for the terminating NUL, and overflow is a hard failure
   rather than a truncated parse: a clipped JSON body would misparse silently. */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} resp_buf;

/* libcurl hands us the body in chunks. Reject the whole transfer once it would
   exceed the buffer by returning a short count, which makes curl_easy_perform
   fail rather than letting us keep a partial body. */
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    resp_buf *r = (resp_buf *)userdata;
    size_t n = size * nmemb;
    if (r->len + n > r->cap) return 0;
    memcpy(r->buf + r->len, ptr, n);
    r->len += n;
    return n;
}

/* Headers every GitHub call needs. GitHub rejects requests without a
   User-Agent, and we want JSON back rather than the form-encoded default the
   token endpoint would otherwise return. */
static struct curl_slist *common_headers(struct curl_slist *h) {
    h = curl_slist_append(h, "Accept: application/json");
    return h;
}

/* The shared tail of both the POST and GET helpers: set the options every
   transfer needs and run it. Keeping this in one place stops the timeout and
   user-agent choices from drifting apart between the two callers. */
static bool perform(CURL *curl, struct curl_slist *headers, resp_buf *r) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mini-gh-tracker");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    /* A stalled connection must not freeze the menu. 10s to connect and 30s
       total keeps a dead network from hanging any of the callers below. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(curl);
    return rc == CURLE_OK;
}

/* POST a form-encoded body and capture the response. out is always
   NUL-terminated on success so json_field can scan it as a C string. */
static bool http_post_form(const char *url, const char *body, char *out, size_t outlen) {
    if (outlen == 0) return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    resp_buf r = { out, 0, outlen - 1 };
    struct curl_slist *headers = common_headers(NULL);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    bool ok = perform(curl, headers, &r);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (!ok) return false;
    out[r.len] = '\0';
    return true;
}

/* GET with a bearer token, for the authenticated GitHub API. The device flow
   never calls this; it backs the user and repo fetches below. http_status is
   set to the HTTP response code when the transfer completes, or left at 0
   when the transfer itself failed, so a caller can tell "GitHub answered
   with an error" apart from "we never reached GitHub". */
static bool
http_get_bearer(const char *url, const char *token, char *out, size_t outlen,
                long *http_status) {
    if (http_status) *http_status = 0;
    if (outlen == 0) return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    resp_buf r = { out, 0, outlen - 1 };
    /* The token rides only in this Authorization header, never in the URL or a
       log line, so it stays out of shell history and any server access log. */
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    struct curl_slist *headers = common_headers(NULL);
    headers = curl_slist_append(headers, auth);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    bool ok = perform(curl, headers, &r);
    if (ok && http_status)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (!ok) return false;
    out[r.len] = '\0';
    return true;
}

/* Classify a token-endpoint reply with no network of its own. A present
   access_token means success; otherwise the error string GitHub returns during
   a device flow selects the matching status, and anything unrecognized falls
   through to GH_ERROR. */
gh_status_t github_parse_token_response(const char *body, char *token_out, size_t outlen) {
    if (json_field(body, "access_token", token_out, outlen)) return GH_OK;
    char err[64];
    if (!json_field(body, "error", err, sizeof(err))) return GH_ERROR;
    if (strcmp(err, "authorization_pending") == 0) return GH_PENDING;
    if (strcmp(err, "slow_down") == 0) return GH_SLOW_DOWN;
    if (strcmp(err, "expired_token") == 0) return GH_EXPIRED;
    if (strcmp(err, "access_denied") == 0) return GH_DENIED;
    return GH_ERROR;
}

/* Open the device flow by requesting a code pair from GitHub. The client id and
   scope are read from the environment (GH_CLIENT_ID, GH_SCOPE) so no secret is
   compiled into the binary and another deployment can supply its own. A missing
   client id is a hard failure; an unset scope defaults to read:user. */
gh_status_t github_device_start(gh_device_t *out) {
    const char *client_id = getenv("GH_CLIENT_ID");
    if (!client_id || !*client_id) return GH_ERROR;
    const char *scope = getenv("GH_SCOPE");
    if (!scope || !*scope) scope = "read:user";

    char body[256];
    snprintf(body, sizeof(body), "client_id=%s&scope=%s", client_id, scope);

    char resp[2048];
    if (!http_post_form("https://github.com/login/device/code", body, resp, sizeof(resp)))
        return GH_ERROR;

    if (!json_field(resp, "device_code", out->device_code, sizeof(out->device_code)))
        return GH_ERROR;
    if (!json_field(resp, "user_code", out->user_code, sizeof(out->user_code)))
        return GH_ERROR;
    if (!json_field(resp, "verification_uri", out->verification_uri, sizeof(out->verification_uri)))
        return GH_ERROR;
    out->interval = (int)json_field_int(resp, "interval", 5);
    return GH_OK;
}

/* One poll of the token endpoint using the device_code from the start call. The
   caller repeats this on GH_PENDING and GH_SLOW_DOWN until the person finishes
   in the browser or the code expires. */
gh_status_t github_device_poll(const char *device_code, char *token_out, size_t outlen) {
    const char *client_id = getenv("GH_CLIENT_ID");
    if (!client_id || !*client_id) return GH_ERROR;

    /* The grant_type is a fixed OAuth constant and the two values are ASCII
       tokens GitHub gave us, so no URL-encoding is needed for this body. */
    char body[256];
    snprintf(body, sizeof(body),
             "client_id=%s&device_code=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code",
             client_id, device_code);

    char resp[2048];
    if (!http_post_form("https://github.com/login/oauth/access_token", body, resp, sizeof(resp)))
        return GH_ERROR;
    return github_parse_token_response(resp, token_out, outlen);
}

/* Fetch the authenticated user with the token and mirror the result into our
   local users table. The header spells out the field handling; the inline notes
   below cover the rejection check and the null-name fallback. */
bool github_fetch_and_upsert_user(const char *token, user_t *out, bool *rejected) {
    char resp[8192];
    long status = 0;
    bool ok = http_get_bearer("https://api.github.com/user", token, resp, sizeof(resp), &status);
    /* status stays 0 on a transport failure, so this is only true when we
       actually reached GitHub and it answered 401/403: a real rejection of
       the token, not a stalled connection or a DNS failure. */
    if (rejected) *rejected = (status == 401 || status == 403);
    if (!ok) return false;

    long long id = json_field_int(resp, "id", 0);
    char login[USERNAME_LEN];
    if (id == 0 || !json_field(resp, "login", login, sizeof(login)))
        return false;

    /* GitHub allows a null name, and json_field copies the literal text for a
       non-string value, so a null shows up as the string "null" here. Either
       way, fall back to the login as the display name. */
    char name[DISPLAY_NAME_LEN];
    if (!json_field(resp, "name", name, sizeof(name)) || strcmp(name, "null") == 0)
        snprintf(name, sizeof(name), "%s", login);

    char avatar[AVATAR_URL_LEN];
    if (!json_field(resp, "avatar_url", avatar, sizeof(avatar)))
        avatar[0] = '\0';

    return db_user_upsert_github(id, login, name, avatar, out);
}

/* Lift the name field out of each object in a /user/repos body. A thin wrapper
   over the array walker, kept on its own so the parse can be tested without a
   live request. */
int github_parse_repo_names(const char *body, char names[][128], int max) {
    return json_array_objects(body, "name", names, max);
}

/* Fetch the user's repositories -- public and private alike -- and hand
   back just their names. visibility=all is spelled out explicitly rather
   than left to GitHub's default so this keeps returning both kinds even if
   that default ever changes. */
int github_list_repos(const char *token, char names[][128], int max) {
    /* A page of full repo objects can run past a few hundred KB, too big for
       a comfortable stack buffer, so this one is heap allocated. per_page is
       kept well under a page's worst case and the cap gives it headroom. */
    size_t cap = 1048576;
    char *resp = malloc(cap);
    if (!resp) return -1;
    bool ok = http_get_bearer(
        "https://api.github.com/user/repos?per_page=30&sort=updated&visibility=all",
        token, resp, cap, NULL);
    int n = ok ? github_parse_repo_names(resp, names, max) : -1;
    free(resp);
    return n;
}

/* GET with no auth at all, for public endpoints like GitHub's user search
   that don't need or want a token. Kept separate from http_get_bearer so a
   caller here can never accidentally leak the signed-in user's token into
   an unauthenticated request. */
static bool http_get_public(const char *url, char *out, size_t outlen) {
    if (outlen == 0) return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    resp_buf r = { out, 0, outlen - 1 };
    struct curl_slist *headers = common_headers(NULL);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    bool ok = perform(curl, headers, &r);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (!ok) return false;
    out[r.len] = '\0';
    return true;
}

/* GET /search/users?q=... and copy up to max matching logins into out. See
   github.h for the full contract. */
int github_search_usernames(const char *query, char logins[][128], int max) {
    if (!query || !query[0] || max <= 0) return 0;

    CURL *esc = curl_easy_init();
    if (!esc) return -1;
    char *enc_q = curl_easy_escape(esc, query, 0);
    if (!enc_q) { curl_easy_cleanup(esc); return -1; }

    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com/search/users?q=%s&per_page=%d", enc_q, max);
    curl_free(enc_q);
    curl_easy_cleanup(esc);

    /* A page of search results (login, id, avatar_url, html_url, etc. per
       hit) comfortably fits a stack-sized-but-heap-allocated buffer even at
       a generous per_page. */
    size_t cap = 65536;
    char *resp = malloc(cap);
    if (!resp) return -1;

    bool ok = http_get_public(url, resp, cap);
    int n = ok ? json_array_field_objects(resp, "items", "login", logins, max) : -1;
    free(resp);
    return n;
}

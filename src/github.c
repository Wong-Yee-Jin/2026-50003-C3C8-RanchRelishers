#include "github.h"
#include "json.h"
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
   token endpoint would otherwise return. Returns NULL on allocation failure,
   which callers must treat as a hard failure, not as "no extra headers". */
static struct curl_slist *common_headers(struct curl_slist *h) {
    return curl_slist_append(h, "Accept: application/json");
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
    if (!headers) {
        curl_easy_cleanup(curl);
        return false;
    }
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
    int auth_len = snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    /* Unreachable with any real GitHub token, but this file's rule is that a
       truncated buffer is a hard failure everywhere, not just where it is
       likely: a silently clipped token would be sent as a different, wrong
       one instead of failing loudly. */
    if (auth_len < 0 || (size_t)auth_len >= sizeof(auth)) {
        curl_easy_cleanup(curl);
        return false;
    }
    struct curl_slist *headers = common_headers(NULL);
    if (!headers) {
        curl_easy_cleanup(curl);
        return false;
    }
    /* curl_slist_append can hand back NULL on allocation failure, and on that
       path it leaves the list we passed in untouched, so headers still owns
       it and has to be freed here rather than lost. Falling through with
       headers unchanged would send the request with no Authorization header
       instead of failing, which is worse than not sending it at all. */
    struct curl_slist *with_auth = curl_slist_append(headers, auth);
    if (!with_auth) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }
    headers = with_auth;
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

/* curl_easy_escape only ignores a NULL handle from libcurl 7.82.0 onward; the
   README targets Ubuntu 22.04, which ships 7.81.0. So every escape here runs
   against a scratch handle opened just for that, never NULL. */
static bool
url_encode(const char *in, char *out, size_t outlen) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    char *enc = curl_easy_escape(curl, in, 0);
    bool ok = false;
    if (enc) {
        int len = snprintf(out, outlen, "%s", enc);
        ok = len >= 0 && (size_t)len < outlen;
    }
    curl_free(enc);
    curl_easy_cleanup(curl);
    return ok;
}

bool gh_build_device_body(const char *client_id, const char *scope, char *out, size_t outlen) {
    char enc_id[256], enc_scope[256];
    if (!url_encode(client_id, enc_id, sizeof(enc_id))) return false;
    if (!url_encode(scope, enc_scope, sizeof(enc_scope))) return false;
    /* A truncated body is a malformed request, not a smaller valid one: fail
       here rather than let it out and have it read like a network error. */
    int len = snprintf(out, outlen, "client_id=%s&scope=%s", enc_id, enc_scope);
    return len >= 0 && (size_t)len < outlen;
}

bool gh_build_poll_body(const char *client_id, const char *device_code, char *out, size_t outlen) {
    char enc_id[256], enc_code[256];
    if (!url_encode(client_id, enc_id, sizeof(enc_id))) return false;
    if (!url_encode(device_code, enc_code, sizeof(enc_code))) return false;
    int len = snprintf(out, outlen,
             "client_id=%s&device_code=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code",
             enc_id, enc_code);
    return len >= 0 && (size_t)len < outlen;
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

    char body[512];
    if (!gh_build_device_body(client_id, scope, body, sizeof(body))) return GH_ERROR;

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

    char body[512];
    if (!gh_build_poll_body(client_id, device_code, body, sizeof(body))) return GH_ERROR;

    char resp[2048];
    if (!http_post_form("https://github.com/login/oauth/access_token", body, resp, sizeof(resp)))
        return GH_ERROR;
    return github_parse_token_response(resp, token_out, outlen);
}

/* Fetch the authenticated user's profile. The header spells out the field
   handling; the inline notes below cover the rejection check and the null-name
   fallback. Whoever calls this decides what to do with the result, which is why
   nothing here knows the users table exists. */
bool github_fetch_user(const char *token, gh_profile_t *out, bool *rejected) {
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

    /* Filled in last, so a half-written profile never escapes the failures
       above with some fields from this response and the rest left as junk. */
    out->id = id;
    snprintf(out->login, sizeof(out->login), "%s", login);
    snprintf(out->display_name, sizeof(out->display_name), "%s", name);
    if (!json_field(resp, "avatar_url", out->avatar_url, sizeof(out->avatar_url)))
        out->avatar_url[0] = '\0';
    return true;
}

/* Lift the name field out of each object in a /user/repos body. A thin wrapper
   over the array walker, kept on its own so the parse can be tested without a
   live request. */
int github_parse_repo_names(const char *body, char names[][128], int max) {
    return json_array_objects(body, "name", names, max);
}

/* Fetch the user's repositories and hand back just their names. */
int github_list_repos(const char *token, char names[][128], int max) {
    /* A page of full repo objects can run past a few hundred KB, too big for
       a comfortable stack buffer, so this one is heap allocated. per_page is
       kept well under a page's worst case and the cap gives it headroom. */
    size_t cap = 1048576;
    char *resp = malloc(cap);
    if (!resp) return -1;
    bool ok = http_get_bearer("https://api.github.com/user/repos?per_page=30&sort=updated",
                              token, resp, cap, NULL);
    int n = ok ? github_parse_repo_names(resp, names, max) : -1;
    free(resp);
    return n;
}

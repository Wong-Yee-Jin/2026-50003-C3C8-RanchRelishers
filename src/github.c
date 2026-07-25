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
   token endpoint would otherwise return. */
static struct curl_slist *common_headers(struct curl_slist *h) {
    h = curl_slist_append(h, "Accept: application/json");
    return h;
}

static bool perform(CURL *curl, struct curl_slist *headers, resp_buf *r) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mini-gh-tracker");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
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

/* GET with a bearer token, for the authenticated GitHub API. Kept alongside
   the device-flow POST because the repo listing built on top of this module
   needs it; the device flow itself never calls it, so it reads as unused until
   that milestone wires it up. */
static bool __attribute__((unused))
http_get_bearer(const char *url, const char *token, char *out, size_t outlen) {
    if (outlen == 0) return false;
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    resp_buf r = { out, 0, outlen - 1 };
    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
    struct curl_slist *headers = common_headers(NULL);
    headers = curl_slist_append(headers, auth);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    bool ok = perform(curl, headers, &r);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (!ok) return false;
    out[r.len] = '\0';
    return true;
}

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

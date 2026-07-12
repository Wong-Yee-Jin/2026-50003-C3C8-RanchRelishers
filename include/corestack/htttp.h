#ifndef CORESTACK_HTTTP_H
#define CORESTACK_HTTTP_H

#include "corestack/secure_session.h"

#define HTTTP_MAX_METHOD  8
#define HTTTP_MAX_PATH    512
#define HTTTP_MAX_HEADERS 32
#define HTTTP_MAX_BODY    8192
#define HTTTP_MAX_EXTRA_HEADERS 1024

typedef struct {
    char key[64];
    char value[256];
} http_header_t;

typedef struct {
    char method[HTTTP_MAX_METHOD];
    char path[HTTTP_MAX_PATH];
    char query[HTTTP_MAX_PATH];
    http_header_t headers[HTTTP_MAX_HEADERS];
    int  header_count;
    char body[HTTTP_MAX_BODY];
    int  body_len;
} http_request_t;

typedef struct {
    int  status_code;
    char content_type[64];
    char *body;
    int  body_len;
    char location[HTTTP_MAX_PATH];
    char extra_headers[HTTTP_MAX_EXTRA_HEADERS];
} http_response_t;

/* Reads and parses one HTTP request off an established secure session.
 * Returns 0 on success, -1 on parse/connection error. */
int htttp_parse_request(session_t *s, http_request_t *out);

/* Serialises and sends an http_response_t back over the session. */
int htttp_send_response(session_t *s, const http_response_t *resp);

/* Convenience builders */
void http_response_html(http_response_t *r, int status, const char *html);
void http_response_redirect(http_response_t *r, const char *location);

/* Extract a URL-decoded form field ("key=value&...") from a request body
 * or query string. Returns 1 if found and copies into out (size outlen),
 * else 0. */
int htttp_form_get(const char *urlencoded, const char *key, char *out, int outlen);

/* Appends one raw header line (no trailing CRLF in header_line, e.g.
 * "Set-Cookie: session_token=abc; Path=/; HttpOnly") to the response.
 * Call this AFTER http_response_html()/http_response_redirect(), since
 * both of those memset() the whole response struct first. Safe to call
 * more than once per response (e.g. clear one cookie, set another). */
void http_response_add_header(http_response_t *r, const char *header_line);

/* Reads cookie `name` out of the request's Cookie header, if present.
 * Returns 1 and copies the raw value into out (size outlen), else 0. */
int htttp_get_cookie(const http_request_t *req, const char *name, char *out, int outlen);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "corestack/htttp.h"

#define RAW_BUF_SIZE 16384

static int url_decode(const char *src, char *dst, int dst_size) {
    int i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return j;
}

int htttp_form_get(const char *urlencoded, const char *key, char *out, int outlen) {
    if (!urlencoded) return 0;
    size_t keylen = strlen(key);
    const char *p = urlencoded;
    while (p && *p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!eq) break;
        size_t field_len = eq - p;
        if (field_len == keylen && strncmp(p, key, keylen) == 0) {
            const char *val_start = eq + 1;
            size_t val_len = amp ? (size_t)(amp - val_start) : strlen(val_start);
            char tmp[HTTTP_MAX_BODY];
            if (val_len >= sizeof(tmp)) val_len = sizeof(tmp) - 1;
            strncpy(tmp, val_start, val_len);
            tmp[val_len] = '\0';
            url_decode(tmp, out, outlen);
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

int htttp_parse_request(session_t *s, http_request_t *out) {
    memset(out, 0, sizeof(*out));

    static char raw[RAW_BUF_SIZE];
    int total = 0;
    int header_end = -1;

    /* Read until we see the blank line terminating headers */
    while (total < RAW_BUF_SIZE - 1) {
        ssize_t n = session_read(s, raw + total, RAW_BUF_SIZE - 1 - total);
        if (n <= 0) return -1;
        total += (int)n;
        raw[total] = '\0';
        char *marker = strstr(raw, "\r\n\r\n");
        if (marker) { header_end = (int)(marker - raw); break; }
    }
    if (header_end < 0) return -1;

    /* ---- request line ---- */
    char *line_end = strstr(raw, "\r\n");
    if (!line_end) return -1;
    char reqline[HTTTP_MAX_PATH + HTTTP_MAX_METHOD + 16];
    int reqline_len = (int)(line_end - raw);
    if (reqline_len >= (int)sizeof(reqline)) reqline_len = sizeof(reqline) - 1;
    strncpy(reqline, raw, reqline_len);
    reqline[reqline_len] = '\0';

    char full_path[HTTTP_MAX_PATH];
    if (sscanf(reqline, "%7s %511s", out->method, full_path) != 2) return -1;

    char *qmark = strchr(full_path, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(out->query, qmark + 1, sizeof(out->query) - 1);
    }
    strncpy(out->path, full_path, sizeof(out->path) - 1);

    /* ---- headers ---- */
    char *cursor = line_end + 2;
    int content_length = 0;
    while (cursor < raw + header_end) {
        char *next = strstr(cursor, "\r\n");
        if (!next || next > raw + header_end) break;
        int len = (int)(next - cursor);
        if (len > 0 && out->header_count < HTTTP_MAX_HEADERS) {
            char hline[320];
            if (len >= (int)sizeof(hline)) len = sizeof(hline) - 1;
            strncpy(hline, cursor, len);
            hline[len] = '\0';
            char *colon = strchr(hline, ':');
            if (colon) {
                *colon = '\0';
                char *val = colon + 1;
                while (*val == ' ') val++;
                strncpy(out->headers[out->header_count].key, hline, 63);
                strncpy(out->headers[out->header_count].value, val, 255);
                if (strcasecmp(hline, "Content-Length") == 0)
                    content_length = atoi(val);
                out->header_count++;
            }
        }
        cursor = next + 2;
    }

    /* ---- body ---- */
    int body_start = header_end + 4;
    int have = total - body_start;
    if (content_length > 0) {
        if (content_length >= HTTTP_MAX_BODY) content_length = HTTTP_MAX_BODY - 1;
        while (have < content_length && total < RAW_BUF_SIZE - 1) {
            ssize_t n = session_read(s, raw + total, RAW_BUF_SIZE - 1 - total);
            if (n <= 0) break;
            total += (int)n;
            have = total - body_start;
        }
        int copy_len = have < content_length ? have : content_length;
        if (copy_len > 0) {
            memcpy(out->body, raw + body_start, copy_len);
            out->body[copy_len] = '\0';
            out->body_len = copy_len;
        }
    }

    return 0;
}

int htttp_send_response(session_t *s, const http_response_t *resp) {
    char header[1024 + HTTTP_MAX_EXTRA_HEADERS];
    const char *status_text =
        resp->status_code == 200 ? "OK" :
        resp->status_code == 302 ? "Found" :
        resp->status_code == 400 ? "Bad Request" :
        resp->status_code == 404 ? "Not Found" :
        resp->status_code == 502 ? "Bad Gateway" : "Internal Server Error";

    /* resp->extra_headers (if any) holds complete "Name: value\r\n"
     * lines, e.g. Set-Cookie -- see http_response_add_header(). It's
     * inserted right before the final Connection/blank-line so it
     * applies to both redirect and normal responses. */
    int hlen;
    if (resp->status_code == 302) {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Location: %s\r\n"
            "Content-Length: 0\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            resp->status_code, status_text, resp->location, resp->extra_headers);
    } else {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            resp->status_code, status_text, resp->content_type, resp->body_len,
            resp->extra_headers);
    }

    if (session_write(s, header, hlen) <= 0) return -1;
    if (resp->body_len > 0 && resp->body)
        if (session_write(s, resp->body, resp->body_len) <= 0) return -1;
    return 0;
}

void http_response_html(http_response_t *r, int status, const char *html) {
    memset(r, 0, sizeof(*r));
    r->status_code = status;
    strncpy(r->content_type, "text/html; charset=utf-8", sizeof(r->content_type) - 1);
    r->body_len = (int)strlen(html);
    r->body = malloc(r->body_len + 1);
    memcpy(r->body, html, r->body_len + 1);
}

void http_response_json(http_response_t *r, int status, const char *json) {
    memset(r, 0, sizeof(*r));
    r->status_code = status;
    strncpy(r->content_type, "application/json; charset=utf-8", sizeof(r->content_type) - 1);
    r->body_len = (int)strlen(json);
    r->body = malloc(r->body_len + 1);
    memcpy(r->body, json, r->body_len + 1);
}

void http_response_redirect(http_response_t *r, const char *location) {
    memset(r, 0, sizeof(*r));
    r->status_code = 302;
    strncpy(r->location, location, sizeof(r->location) - 1);
}

void http_response_add_header(http_response_t *r, const char *header_line) {
    size_t cur = strlen(r->extra_headers);
    size_t add = strlen(header_line);
    /* +3 for "\r\n" plus the NUL */
    if (cur + add + 3 >= sizeof(r->extra_headers)) return; /* header block full; drop silently */
    int n = snprintf(r->extra_headers + cur, sizeof(r->extra_headers) - cur, "%s\r\n", header_line);
    if (n < 0) r->extra_headers[cur] = '\0';
}

int htttp_get_cookie(const http_request_t *req, const char *name, char *out, int outlen) {
    const char *cookie_header = NULL;
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Cookie") == 0) {
            cookie_header = req->headers[i].value;
            break;
        }
    }
    if (!cookie_header) return 0;

    size_t namelen = strlen(name);
    const char *p = cookie_header;
    while (p && *p) {
        while (*p == ' ') p++;
        const char *eq = strchr(p, '=');
        const char *semi = strchr(p, ';');
        if (!eq || (semi && eq > semi)) break;

        size_t field_len = (size_t)(eq - p);
        if (field_len == namelen && strncmp(p, name, namelen) == 0) {
            const char *val_start = eq + 1;
            int val_len = semi ? (int)(semi - val_start) : (int)strlen(val_start);
            if (val_len >= outlen) val_len = outlen - 1;
            if (val_len < 0) val_len = 0;
            strncpy(out, val_start, val_len);
            out[val_len] = '\0';
            return 1;
        }
        if (!semi) break;
        p = semi + 1;
    }
    return 0;
}

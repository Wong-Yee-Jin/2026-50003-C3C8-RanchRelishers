#define _GNU_SOURCE /* strcasestr */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include "oauth_github.h"

#define GH_TOKEN_HOST "github.com"
#define GH_TOKEN_PATH "/login/oauth/access_token"
#define GH_API_HOST "api.github.com"
#define GH_USER_PATH "/user"

static const char *env_or(const char *name, const char *dflt) {
    const char *v = getenv(name);
    return (v && v[0]) ? v : dflt;
}

static void url_encode(const char *src, char *dst, int dst_size) {
    static const char *hex = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

static bool json_get_string(const char *json, const char *key, char *out, int outlen) {
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false; /* null, number, nested object, etc -- not a plain string */
    p++;

    int j = 0;
    while (*p && *p != '"' && j < outlen - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            out[j++] = (*p == 'n') ? '\n' : (*p == 't') ? '\t' : *p;
        } else {
            out[j++] = *p;
        }
        p++;
    }
    out[j] = '\0';
    return true;
}

static bool json_get_int64(const char *json, const char *key, long long *out) {
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char *end;
    long long v = strtoll(p, &end, 10);
    if (end == p) return false;
    *out = v;
    return true;
}

/* ---------------------------------------------------------------------
 * minimal outbound HTTPS client (TLS client handshake + raw HTTP/1.1)
 * --------------------------------------------------------------------- */

static SSL_CTX *g_client_ctx = NULL;

static SSL_CTX *client_ctx(void) {
    if (!g_client_ctx) {
        SSL_library_init();
        SSL_load_error_strings();
        g_client_ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_min_proto_version(g_client_ctx, TLS1_2_VERSION);
        SSL_CTX_set_default_verify_paths(g_client_ctx); /* trust system CA bundle */
        SSL_CTX_set_verify(g_client_ctx, SSL_VERIFY_PEER, NULL);
    }
    return g_client_ctx;
}

/* Connects to host:443 over TLS (with SNI + hostname verification),
 * sends `request` verbatim, reads the full response until the peer
 * closes the connection, and returns it as a heap string (caller
 * frees). Returns NULL on any connection/handshake/verification
 * error. */
static char *https_roundtrip(const char *host, const char *request) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &res) != 0) return NULL;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        fprintf(stderr, "[oauth] could not connect to %s:443\n", host);
        return NULL;
    }

    SSL *ssl = SSL_new(client_ctx());
    SSL_set_tlsext_host_name(ssl, host); /* SNI */
    SSL_set1_host(ssl, host);            /* verify the cert matches this hostname */
    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "[oauth] TLS handshake/verification with %s failed\n", host);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        return NULL;
    }

    size_t req_len = strlen(request);
    if (SSL_write(ssl, request, (int)req_len) <= 0) {
        SSL_free(ssl);
        close(fd);
        return NULL;
    }

    size_t cap = 16384, len = 0;
    char *buf = malloc(cap);
    for (;;) {
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        int n = SSL_read(ssl, buf + len, (int)(cap - len - 1));
        if (n <= 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    return buf;
}

/* Splits a raw HTTP response (status line + headers + body) sitting in
 * `raw` into a status code and a pointer to the body (still inside
 * `raw` -- do not free separately). Transparently undoes chunked
 * transfer-encoding into a static scratch buffer if present. */
static bool split_http_response(char *raw, int *status_out, char **body_out) {
    if (strncmp(raw, "HTTP/1.", 7) != 0) return false;
    *status_out = atoi(raw + 9);

    char *marker = strstr(raw, "\r\n\r\n");
    if (!marker) return false;
    *body_out = marker + 4;

    if (strcasestr(raw, "\r\nTransfer-Encoding: chunked\r\n") != NULL) {
        static char dechunked[32768];
        char *src = *body_out;
        size_t out_len = 0;
        while (*src) {
            char *next;
            long chunk_len = strtol(src, &next, 16);
            if (next == src || chunk_len <= 0) break;
            src = next;
            while (*src == '\r' || *src == '\n') src++;
            if (out_len + (size_t)chunk_len >= sizeof(dechunked)) break;
            memcpy(dechunked + out_len, src, (size_t)chunk_len);
            out_len += (size_t)chunk_len;
            src += chunk_len;
            while (*src == '\r' || *src == '\n') src++;
        }
        dechunked[out_len] = '\0';
        *body_out = dechunked;
    }
    return true;
}

/* ---------------------------------------------------------------------
 * public API
 * --------------------------------------------------------------------- */

bool oauth_github_authorize_url(const char *state, char *out, int outlen) {
    const char *client_id = getenv("GITHUB_CLIENT_ID");
    if (!client_id || !client_id[0]) return false;
    const char *base_url = env_or("APP_BASE_URL", "https://localhost:8443");

    char redirect_uri[256];
    snprintf(redirect_uri, sizeof(redirect_uri), "%s/auth/github/callback", base_url);

    char enc_redirect[384], enc_state[128];
    url_encode(redirect_uri, enc_redirect, sizeof(enc_redirect));
    url_encode(state, enc_state, sizeof(enc_state));

    snprintf(out, outlen,
             "https://github.com/login/oauth/authorize"
             "?client_id=%s&redirect_uri=%s&scope=read:user&state=%s",
             client_id, enc_redirect, enc_state);
    return true;
}

bool oauth_github_exchange_code(const char *code, char *token_out, int token_outlen) {
    const char *client_id     = getenv("GITHUB_CLIENT_ID");
    const char *client_secret = getenv("GITHUB_CLIENT_SECRET");
    if (!client_id || !client_id[0] || !client_secret || !client_secret[0] || !code || !code[0])
        return false;

    const char *base_url = env_or("APP_BASE_URL", "https://localhost:8443");
    char redirect_uri[256];
    snprintf(redirect_uri, sizeof(redirect_uri), "%s/auth/github/callback", base_url);

    char enc_id[128], enc_secret[128], enc_code[256], enc_redirect[384];
    url_encode(client_id, enc_id, sizeof(enc_id));
    url_encode(client_secret, enc_secret, sizeof(enc_secret));
    url_encode(code, enc_code, sizeof(enc_code));
    url_encode(redirect_uri, enc_redirect, sizeof(enc_redirect));

    char body[1200];
    int body_len = snprintf(body, sizeof(body),
        "client_id=%s&client_secret=%s&code=%s&redirect_uri=%s",
        enc_id, enc_secret, enc_code, enc_redirect);

    char request[1600];
    snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: application/json\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %d\r\n"
        "User-Agent: mini-gh-tracker\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        GH_TOKEN_PATH, GH_TOKEN_HOST, body_len, body);

    char *raw = https_roundtrip(GH_TOKEN_HOST, request);
    if (!raw) return false;

    int status = 0;
    char *json = NULL;
    bool ok = split_http_response(raw, &status, &json) && status == 200 &&
              json_get_string(json, "access_token", token_out, token_outlen);
    if (!ok)
        fprintf(stderr, "[oauth] token exchange failed (http %d)\n", status);

    free(raw);
    return ok;
}

bool oauth_github_fetch_user(const char *access_token, gh_user_t *out) {
    memset(out, 0, sizeof(*out));
    if (!access_token || !access_token[0]) return false;

    char request[512];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Accept: application/vnd.github+json\r\n"
        "User-Agent: mini-gh-tracker\r\n"
        "Connection: close\r\n\r\n",
        GH_USER_PATH, GH_API_HOST, access_token);

    char *raw = https_roundtrip(GH_API_HOST, request);
    if (!raw) return false;

    int status = 0;
    char *json = NULL;
    bool ok = split_http_response(raw, &status, &json) && status == 200;
    if (ok) {
        ok = json_get_int64(json, "id", &out->id) && out->id != 0;
        json_get_string(json, "login", out->login, sizeof(out->login));
        json_get_string(json, "name", out->name, sizeof(out->name));
        json_get_string(json, "avatar_url", out->avatar_url, sizeof(out->avatar_url));
        if (ok && !out->login[0]) ok = false;
    } else {
        fprintf(stderr, "[oauth] fetching GitHub profile failed (http %d)\n", status);
    }

    free(raw);
    return ok;
}

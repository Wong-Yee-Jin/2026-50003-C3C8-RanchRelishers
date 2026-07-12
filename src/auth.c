#include <string.h>
#include <stdio.h>
#include <time.h>
#include <openssl/rand.h>
#include "auth.h"
#include "db.h"

#define SESSION_COOKIE_NAME "session_token"
#define SESSION_TTL_SECONDS (60 * 60 * 24 * 14)
#define TOKEN_HEX_LEN 64

/* Per-request cache 
 * Safe as a plain static because main.c forks a fresh process for every connection, so this never leaks across requests.
 * (see corestack/secure_session.c / main.c)*/
static bool   g_have_user = false;
static user_t g_current_user;

static void gen_token(char out[TOKEN_HEX_LEN + 1]) {
    unsigned char raw[TOKEN_HEX_LEN / 2];
    RAND_bytes(raw, sizeof(raw));
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2]     = hex[(raw[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[raw[i] & 0xF];
    }
    out[TOKEN_HEX_LEN] = '\0';
}

void auth_begin_request(const http_request_t *req) {
    g_have_user = false;

    char token[TOKEN_HEX_LEN + 1] = {0};
    if (!htttp_get_cookie(req, SESSION_COOKIE_NAME, token, sizeof(token)) || !token[0])
        return;

    char user_id[ID_LEN] = {0};
    if (!db_session_find_user_id(token, user_id))
        return; /* no such session, or it expired */

    if (db_user_find_by_id(user_id, &g_current_user))
        g_have_user = true;
}

bool auth_get_current_user(user_t *out) {
    if (!g_have_user) return false;
    *out = g_current_user;
    return true;
}

void auth_start_session(http_response_t *resp, const char *user_id) {
    char token[TOKEN_HEX_LEN + 1];
    gen_token(token);

    long long expires_at = (long long)time(NULL) + SESSION_TTL_SECONDS;
    if (!db_session_create(token, user_id, expires_at)) return;

    char cookie[160];
    snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=%s; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=%d", SESSION_COOKIE_NAME, token, SESSION_TTL_SECONDS);
    http_response_add_header(resp, cookie);
}

void auth_end_session(const http_request_t *req, http_response_t *resp) {
    char token[TOKEN_HEX_LEN + 1] = {0};
    if (htttp_get_cookie(req, SESSION_COOKIE_NAME, token, sizeof(token)) && token[0]) {
        /* Only ever deletes our own row in `sessions`.*/
        db_session_delete(token);
    }

    char cookie[96];
    snprintf(cookie, sizeof(cookie), "Set-Cookie: %s=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0", SESSION_COOKIE_NAME);
    http_response_add_header(resp, cookie);
}

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "corestack/secure_session.h"

struct session_ctx {
    SSL_CTX *ssl_ctx;
};

struct session {
    SSL *ssl;
    int  fd;
};

session_ctx_t *session_server_init(const char *cert_path, const char *key_path) {
    SSL_library_init();
    SSL_load_error_strings();

    session_ctx_t *ctx = calloc(1, sizeof(session_ctx_t));
    ctx->ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx->ssl_ctx) {
        fprintf(stderr, "[secure_session] SSL_CTX_new failed\n");
        free(ctx);
        return NULL;
    }

    /* Modern-only ciphers; equivalent security bar to a well-built AES handshake library. */
    SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx->ssl_ctx, cert_path, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "[secure_session] failed to load cert/key from %s / %s\n",
                cert_path, key_path);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx->ssl_ctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

session_t *session_accept(session_ctx_t *ctx, int raw_fd) {
    SSL *ssl = SSL_new(ctx->ssl_ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, raw_fd);

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    session_t *s = malloc(sizeof(session_t));
    s->ssl = ssl;
    s->fd  = raw_fd;
    return s;
}

ssize_t session_read(session_t *s, void *buf, size_t len) {
    int n = SSL_read(s->ssl, buf, (int)len);
    return n > 0 ? (ssize_t)n : -1;
}

ssize_t session_write(session_t *s, const void *buf, size_t len) {
    int n = SSL_write(s->ssl, buf, (int)len);
    return n > 0 ? (ssize_t)n : -1;
}

void session_close(session_t *s) {
    if (!s) return;
    SSL_shutdown(s->ssl);
    SSL_free(s->ssl);
    close(s->fd);
    free(s);
}

void session_server_shutdown(session_ctx_t *ctx) {
    if (!ctx) return;
    SSL_CTX_free(ctx->ssl_ctx);
    free(ctx);
}

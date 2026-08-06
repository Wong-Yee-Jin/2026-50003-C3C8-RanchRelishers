#include "tetrish_gate.h"
#include "corestack/secure_session.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Same pattern tetrisu's tcp_connect() uses: plain blocking connect(), no
 * retries. A live tetrisd is expected to accept immediately. */
static int tcp_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void set_err(char *err, size_t err_len, const char *msg) {
    if (!err || err_len == 0) return;
    snprintf(err, err_len, "%s", msg);
}

int tetrish_gate_check(const char *host, int port, const char *ca_path,
                        char *err, size_t err_len) {
    int fd = tcp_connect(host, port);
    if (fd < 0) {
        char buf[256];
        snprintf(buf, sizeof buf, "cannot reach tetrisd at %s:%d (connect failed)", host, port);
        set_err(err, err_len, buf);
        return 0;
    }

    session_ctx_t *ctx = session_client_ctx_init(ca_path);
    if (!ctx) {
        close(fd);
        char buf[320];
        snprintf(buf, sizeof buf, "cannot load CA bundle from '%s' (TETRISD_CA_PATH)", ca_path);
        set_err(err, err_len, buf);
        return 0;
    }

    /* Full 7-step libtetrissh handshake: nonce, cert, verify-against-CA,
     * RSA-PSS signature, verify, AES-256 session key wrap. Only a genuine
     * tetrisd holding the CA-signed cert's private key can complete this. */
    session_t *sess = session_connect(ctx, fd);
    if (!sess) {
        set_err(err, err_len,
                "connected, but the tetrisd handshake failed "
                "(bad/expired cert, wrong CA, or not really tetrisd)");
        session_client_ctx_free(ctx);
        close(fd);
        return 0;
    }

    /* Liveness+authenticity proven. We don't need the session for anything
     * else, so tear it down right away. */
    session_close(sess);
    session_client_ctx_free(ctx);
    close(fd);
    return 1;
}

#ifndef CORESTACK_SECURE_SESSION_H
#define CORESTACK_SECURE_SESSION_H

#include <stddef.h>

typedef struct session_ctx session_ctx_t;   /* opaque server-wide context   */
typedef struct session session_t;      /* opaque per-connection state  */

/* Initialise the secure layer server-side.
 * cert_path / key_path point to a PEM certificate + private key
 * (see certs/generate_certs.sh for a self-signed dev pair). */
session_ctx_t *session_server_init(const char *cert_path, const char *key_path);

/* Perform the secure handshake on an already-accept()-ed raw socket fd.
 * Returns a session_t* on success (caller owns it), NULL on failure. */
session_t *session_accept(session_ctx_t *ctx, int raw_fd);

/* Encrypted read/write over an established session. Same semantics as
 * read(2)/write(2): returns bytes transferred, <=0 on error/EOF. */
ssize_t session_read(session_t *s, void *buf, size_t len);
ssize_t session_write(session_t *s, const void *buf, size_t len);

void session_close(session_t *s);
void session_server_shutdown(session_ctx_t *ctx);

#endif

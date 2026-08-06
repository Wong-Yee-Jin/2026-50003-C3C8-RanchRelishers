#ifndef TETRISH_GATE_H
#define TETRISH_GATE_H

#include <stddef.h> /* size_t */

/*
 * tetrish_gate.h — startup precondition: mini-gh-tracker only runs while a
 * live tetrisd is reachable.
 *
 * This does NOT ping a port or just check "is something listening". It runs
 * the real libtetrissh client handshake (corestack/secure_session.c, the
 * exact object file tetrisd links) against tetrisd: TCP connect, then
 * nonce -> cert -> verify -> signature -> verify -> AES key wrap. If every
 * step succeeds we know a genuine tetrisd (holding the private key for a
 * cert our CA signed) is up right now. The session is torn down immediately
 * afterward; mini-gh-tracker does not keep talking to tetrisd, it only uses
 * the handshake as a liveness+authenticity check before starting its own
 * (unrelated) menu loop.
 *
 * Configuration is via environment variables, consistent with the rest of
 * this project's DB_PATH / GH_CLIENT_ID style:
 *
 *   TETRISD_HOST     - default "127.0.0.1"
 *   TETRISD_PORT     - default 7777 (tetrisd's sample.tetrishrc default)
 *   TETRISD_CA_PATH  - default "auth/cacsertificate.crt"; must be the SAME
 *                       CA bundle the running tetrisd's cert was signed by,
 *                       or the handshake will correctly fail cert
 *                       verification even though tetrisd is up.
 */

/* Attempts the full client handshake against a live tetrisd at
 * host:port, verifying its certificate against ca_path.
 *
 * Returns 1 if tetrisd is up and the handshake completed (mini-gh-tracker
 * may start). Returns 0 if not (host/port unreachable, or a live process
 * answered but failed the handshake) and writes a human-readable reason
 * into err (up to err_len - 1 bytes, NUL-terminated). err may be NULL if
 * the caller doesn't want the reason. */
int tetrish_gate_check(const char *host, int port, const char *ca_path,
                        char *err, size_t err_len);

#endif

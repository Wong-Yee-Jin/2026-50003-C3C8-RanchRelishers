/*
 * main.c
 * ------
 * Boots the whole monolith:
 *   1. Connect to MongoDB (db.c)
 *   2. Init the secure session layer (corestack/secure_session.c)
 *   3. Open a listening TCP socket
 *   4. Register all routes (src/handlers)
 *   5. Accept loop: for each connection -> TLS handshake -> parse one
 *      HTTP request -> dispatch -> respond -> close (fork per connection
 *      for concurrency, matching the "monolithic but not single-request"
 *      style of the reference course server).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

#include "corestack/secure_session.h"
#include "corestack/htttp.h"
#include "router.h"
#include "handlers.h"
#include "db.h"
#include "auth.h"

#define DEFAULT_PORT      8443
#define DEFAULT_MONGO_URI "mongodb://localhost:27017"
#define DEFAULT_DB_NAME   "mini_gh_tracker"
#define DEFAULT_CERT      "certs/server.crt"
#define DEFAULT_KEY       "certs/server.key"

static void reap_children(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

/* Loads simple "KEY=VALUE" lines from a .env file into the process
 * environment, if the file exists -- MONGO_URI, GITHUB_CLIENT_ID,
 * GITHUB_CLIENT_SECRET, APP_BASE_URL, etc, all read via plain
 * getenv() elsewhere (src/main.c, src/oauth_github.c) pick this up
 * automatically since setenv() affects the whole process, not just
 * this function. Real shell-exported env vars still win (setenv's
 * overwrite=0), so `export FOO=bar` always takes precedence over
 * .env. Blank lines and lines starting with '#' are skipped. Missing
 * .env is not an error -- it's optional, e.g. in prod you'd usually
 * set real env vars instead. */
static void load_dotenv(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        char *kend = key + strlen(key);
        while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = '\0';

        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r' ||
                             val[vlen - 1] == ' '  || val[vlen - 1] == '\t')) {
            val[--vlen] = '\0';
        }

        if (key[0]) setenv(key, val, 0);
    }
    fclose(f);
}

static int open_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(fd, 64) < 0) { perror("listen"); exit(1); }
    return fd;
}

static void handle_connection(session_ctx_t *sctx, int client_fd) {
    session_t *s = session_accept(sctx, client_fd);
    if (!s) { close(client_fd); return; }

    http_request_t req;
    if (htttp_parse_request(s, &req) == 0) {
        http_response_t resp = {0};
        auth_begin_request(&req); /* looks up the session cookie, if any */
        router_dispatch(&req, &resp);
        htttp_send_response(s, &resp);
        if (resp.body) free(resp.body);
    }

    session_close(s);
}

int main(int argc, char **argv) {
    load_dotenv(".env"); /* must run before any getenv() calls below/elsewhere */

    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    const char *mongo_uri = getenv("MONGO_URI") ? getenv("MONGO_URI") : DEFAULT_MONGO_URI;

    if (!db_init(mongo_uri, DEFAULT_DB_NAME)) {
        fprintf(stderr, "fatal: could not connect to MongoDB at %s\n", mongo_uri);
        return 1;
    }
    db_labels_seed();

    session_ctx_t *sctx = session_server_init(DEFAULT_CERT, DEFAULT_KEY);
    if (!sctx) {
        fprintf(stderr, "fatal: could not init secure session layer "
                        "(did you run certs/generate_certs.sh?)\n");
        return 1;
    }

    project_handlers_register();
    issue_handlers_register();
    label_handlers_register();
    user_handlers_register();
    auth_handlers_register();

    signal(SIGCHLD, reap_children);
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = open_listener(port);
    printf("mini-gh-tracker listening on https://0.0.0.0:%d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) continue;

        pid_t pid = fork();
        if (pid == 0) {
            close(listen_fd);
            handle_connection(sctx, client_fd);
            _exit(0);
        } else {
            close(client_fd);
        }
    }

    session_server_shutdown(sctx);
    db_shutdown();
    return 0;
}

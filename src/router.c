#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "router.h"

#define MAX_ROUTES 64

typedef struct {
    char method[8];
    char pattern[128];
    route_handler_t handler;
} route_t;

static route_t g_routes[MAX_ROUTES];
static int g_route_count = 0;

void router_add(const char *method, const char *pattern, route_handler_t handler) {
    if (g_route_count >= MAX_ROUTES) return;
    route_t *r = &g_routes[g_route_count++];
    strncpy(r->method, method, sizeof(r->method) - 1);
    strncpy(r->pattern, pattern, sizeof(r->pattern) - 1);
    r->handler = handler;
}

/* Splits "/a/b/c" into up to `max` segments.
 * Returns count. */
static int split_path(const char *path, char segs[][64], int max) {
    int n = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p && n < max) {
        const char *slash = strchr(p, '/');
        int len = slash ? (int)(slash - p) : (int)strlen(p);
        if (len >= 64) len = 63;
        strncpy(segs[n], p, len);
        segs[n][len] = '\0';
        n++;
        if (!slash) break;
        p = slash + 1;
    }
    return n;
}

static int try_match(const route_t *r, const char *path, path_params_t *out_params) {
    char route_segs[8][64], req_segs[8][64];
    int rn = split_path(r->pattern, route_segs, 8);
    int qn = split_path(path, req_segs, 8);
    if (rn != qn) return 0;

    out_params->count = 0;
    for (int i = 0; i < rn; i++) {
        if (route_segs[i][0] == ':') {
            if (out_params->count < MAX_PARAMS) {
                strncpy(out_params->params[out_params->count].key, route_segs[i] + 1, 31);
                strncpy(out_params->params[out_params->count].value, req_segs[i], PARAM_VAL_LEN - 1);
                out_params->count++;
            }
        } else if (strcmp(route_segs[i], req_segs[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

const char *path_param_get(const path_params_t *params, const char *key) {
    for (int i = 0; i < params->count; i++)
        if (strcmp(params->params[i].key, key) == 0)
            return params->params[i].value;
    return NULL;
}

void router_dispatch(const http_request_t *req, http_response_t *resp) {
    path_params_t params;
    for (int i = 0; i < g_route_count; i++) {
        if (strcasecmp(g_routes[i].method, req->method) != 0) continue;
        if (try_match(&g_routes[i], req->path, &params)) {
            g_routes[i].handler(req, resp, &params);
            return;
        }
    }
    http_response_html(resp, 404,
        "<html><body><h1>404 Not Found</h1><p>No such route.</p></body></html>");
}

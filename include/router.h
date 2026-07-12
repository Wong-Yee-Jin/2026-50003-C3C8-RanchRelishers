#ifndef ROUTER_H
#define ROUTER_H

#include "corestack/htttp.h"

#define MAX_PARAMS 4
#define PARAM_VAL_LEN 64

typedef struct {
    char key[32];
    char value[PARAM_VAL_LEN];
} path_param_t;

typedef struct {
    path_param_t params[MAX_PARAMS];
    int count;
} path_params_t;

typedef void (*route_handler_t)(const http_request_t *req, http_response_t *resp, const path_params_t *params);

/* Register a route. pattern segments starting with ':' are captured as params, e.g. "/issues/:id/comments". */
void router_add(const char *method, const char *pattern, route_handler_t handler);

/* Dispatches req to the matching handler, or writes a 404 into resp. */
void router_dispatch(const http_request_t *req, http_response_t *resp);

/* Returns the value for `key` in params, or NULL if absent. */
const char *path_param_get(const path_params_t *params, const char *key);

#endif

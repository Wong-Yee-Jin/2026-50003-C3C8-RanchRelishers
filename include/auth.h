#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include "corestack/htttp.h"
#include "models.h"


/* Looks up the session cookie (if any) against the DB and caches the result for the rest of this request. Call once, right after parsing the request and before router_dispatch(). */
void auth_begin_request(const http_request_t *req);

/* Returns true and fills *out if this request belongs to a logged-in user (per the cache populated by auth_begin_request()). */
bool auth_get_current_user(user_t *out);

/* Creates a new server-side session for user_id and attaches a Set-Cookie header to resp. 
 * Call AFTER http_response_html() or http_response_redirect() to reset the response struct. */
void auth_start_session(http_response_t *resp, const char *user_id);

/* Deletes whatever session the request's cookie points to (if any) and clears the cookie on resp. 
 * Call AFTER http_response_html() or http_response_redirect(). 
 * Github.com is never contacted. */
void auth_end_session(const http_request_t *req, http_response_t *resp);

#endif

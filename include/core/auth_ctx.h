#ifndef AUTH_CTX_H
#define AUTH_CTX_H

#include <stdbool.h>

/* The current signed-in user id, or empty when nobody is signed in. The
   GitHub device flow (M4) sets this after login; until then a local session
   can set a placeholder so the single local user can perform actions. */
void auth_ctx_set_user(const char *user_id);   // "" clears
const char *auth_ctx_user(void);               // returns "" when signed out
bool auth_ctx_is_authed(void);   // the gate every mutating service call checks first

#endif

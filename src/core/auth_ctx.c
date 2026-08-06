#include "core/auth_ctx.h"
#include "models.h"
#include <stdio.h>

/* One process, one logged-in user, so a single static buffer is enough:
   this is a terminal app driven by one local session at a time. Sized to
   ID_LEN because that's the exact width every id in this codebase is
   generated at, the same bound db.c uses for a project, issue, or user row. */
static char CURRENT[ID_LEN] = "";

// Login sets this once the GitHub device flow succeeds; before that lands, a
// local session can call it with a placeholder id so the single user can
// still act. Passing NULL falls through to "", the same as an explicit clear.
void auth_ctx_set_user(const char *user_id) {
    snprintf(CURRENT, sizeof(CURRENT), "%s", user_id ? user_id : "");
}

// Anything that needs to attribute an action back to a person, like adding a
// comment or self-assigning, reads the id here instead of it being threaded
// through every call in the service layer.
const char *auth_ctx_user(void) { return CURRENT; }

// An empty buffer means nobody has signed in. Every mutating service call
// checks this first, so a blank result here is what turns that call away.
bool auth_ctx_is_authed(void) { return CURRENT[0] != '\0'; }

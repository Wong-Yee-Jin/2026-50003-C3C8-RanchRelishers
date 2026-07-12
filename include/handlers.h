#ifndef HANDLERS_H
#define HANDLERS_H

#include <stddef.h>
#include "router.h"

/* Formats minutes as a compact "2h 30m" string (see time_handlers.c).
 * Shared here so issue_handlers.c can render estimate/logged time on the issue detail page without duplicating the logic. */
void time_format_minutes(int minutes, char *out, size_t outlen);

/* Each *_register() wires that module's routes into the router.
 * Called once from main() at startup. */
void project_handlers_register(void);
void issue_handlers_register(void);
void comment_handlers_register(void);
void label_handlers_register(void);
void user_handlers_register(void);
void time_handlers_register(void);
void auth_handlers_register(void);

#endif

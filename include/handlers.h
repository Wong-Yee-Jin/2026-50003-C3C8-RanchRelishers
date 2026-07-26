#ifndef HANDLERS_H
#define HANDLERS_H

#include <stddef.h>
#include "router.h"

/* Each *_register() wires that module's routes into the router.
 * Called once from main() at startup. */
void project_handlers_register(void);
void issue_handlers_register(void);
void label_handlers_register(void);
void user_handlers_register(void);
void auth_handlers_register(void);

#endif

#ifndef UTIL_H
#define UTIL_H
#include <stdbool.h>
#include "models.h"

/* Fill out with a 24-char lowercase hex id and a NUL. Returns false when
   /dev/urandom cannot be read. A predictable id could collide with or guess an
   existing record, so callers must check the result and abort on false rather
   than fall back to a weak id. */
bool id_generate(char out[ID_LEN]);

#endif

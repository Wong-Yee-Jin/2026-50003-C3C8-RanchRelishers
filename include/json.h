#ifndef JSON_H
#define JSON_H
#include <stdbool.h>
#include <stddef.h>

/* A small self-contained JSON scanner for the responses GitHub returns. The
   old code matched fields with strstr, which the migration audit flagged as
   fragile because a key inside a nested object or a string value could be
   mistaken for the real thing. These functions tokenize the input for real,
   honoring backslash escapes inside strings. Object and array nesting is
   tracked so only the level being asked about is searched. */

/* Copy the top-level value for key into out and return true. For a string the
   contents are unescaped; for a number, boolean, or null the literal text is
   copied. Returns false when the input does not parse or the key is absent at
   the top level, so a key that appears only inside a nested object is a miss. */
bool json_field(const char *body, const char *key, char *out, size_t outlen);

/* json_field followed by strtol, returning dflt when the field is absent or
   does not read as an integer. */
long json_field_int(const char *body, const char *key, long dflt);

/* Walk a top-level array and copy each object element's field string into the
   next out row, up to max rows. Returns how many were written. Elements that
   are not objects or lack the field are skipped. */
int json_array_objects(const char *body, const char *field, char out[][128], int max);

/* Same as json_array_objects, but for an array nested one level down under
   array_key -- e.g. GitHub's {"items":[...]} search responses -- instead of
   a bare top-level array. */
int json_array_field_objects(const char *body, const char *array_key,
                              const char *field, char out[][128], int max);

#endif

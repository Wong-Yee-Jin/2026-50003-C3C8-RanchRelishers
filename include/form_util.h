#ifndef FORM_UTIL_H
#define FORM_UTIL_H

#include "models.h"

/*
 * htttp_form_get() (corestack/htttp.h) only ever returns the FIRST value
 * for a given key, which is enough for single text fields. Multi-select
 * assignment (checking several labels or assignees at once) needs every
 * occurrence of a repeated key, e.g. a request body like
 * "label_id=<id1>&label_id=<id2>&label_id=<id3>".
 */

/* Collects every URL-decoded value for `key` found in `urlencoded`
 * (form body or query string) into out[0..return), each capped at
 * ID_LEN-1 chars, stopping after `max` matches. Returns the count found. */
int form_get_all(const char *urlencoded, const char *key, char out[][ID_LEN], int max);

#endif

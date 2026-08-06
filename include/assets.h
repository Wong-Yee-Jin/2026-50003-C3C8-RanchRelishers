#ifndef ASSETS_H
#define ASSETS_H

/* The splash wordmark, one row per string, NULL-terminated so callers can
   walk it without a separate length constant. It is stored once; the frame
   animation in render.c colors a moving column span over this same text
   instead of duplicating the whole block for every frame. */
extern const char *const wordmark[];

/* One-line mark for COMPACT mode and the screen headers. */
extern const char *const logo_compact;

/* Computed from the wordmark array rather than hardcoded, so editing the
   art can never drift out of sync with the geometry test. */
int wordmark_width(void);
int wordmark_height(void);

#endif

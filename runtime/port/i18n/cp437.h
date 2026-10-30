/* The games' own text (code page 437) as UTF-8, for an untranslated string the HD text shows.
   Catalog translations are already strict UTF-8 and never pass through here. */
#ifndef PORT_CP437_H
#define PORT_CP437_H
#include <stddef.h>

/* text's length bytes as UTF-8, NUL-terminated, in a new allocation (free it); NULL when out of
   memory. ASCII stays as it is. */
char *cp437_to_utf8(const char *text, size_t length);

#endif

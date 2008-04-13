#ifndef STRING_CACHE_H
#define STRING_CACHE_H

#include <stddef.h>

/** Cache unique copies of strings.  */
const char * cache_string (const char * str);


/** Hash function.  */
unsigned long string_hash (const char * str);


/** Look-up hash of cached string.  */
unsigned long string_hash_get (const char * str);

#endif

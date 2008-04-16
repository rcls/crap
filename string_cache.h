#ifndef STRING_CACHE_H
#define STRING_CACHE_H

#include <stddef.h>

/** Cache unique copy of a string.  */
const char * cache_string (const char * str);

/** Cache unique copy of a string.  */
const char * cache_string_n (const char * str, size_t len); 

/** Hash function.  */
unsigned long string_hash (const char * str, size_t len);


/** Look-up hash of cached string.  */
unsigned long string_hash_get (const char * str);

#endif

#ifndef STRING_CACHE_H
#define STRING_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** Cache unique copy of a string.  */
const char * cache_string (const char * str);

/** Cache unique copy of a string.  */
const char * cache_string_n (const char * str, size_t len);

/** Hash function.  */
unsigned long string_hash_func (const char * str, size_t len);

/** Look-up hash of cached string.  */
unsigned long string_hash_get (const char * str);

/** Compare of cached string, faster than strcmp when equality is likely.  */
static inline int cache_strcmp (const char * A, const char * B)
{
    return A == B ? 0 : strcmp (A, B);
}

/** Output statistics on the string cache.  */
void string_cache_stats (FILE * f);

/** Free all memory used by the string cache.  */
void string_cache_destroy();


/** Support for hashes indexed by a cached string.  */
typedef struct string_hash_head {
    const char * string;                /* Must be cached.  */
    struct string_hash_head * next;
} string_hash_head_t;


/** A hash table.  */
typedef struct string_hash {
    size_t num_entries;
    size_t num_buckets;                 /* Always a power of two.  */
    string_hash_head_t ** buckets;
} string_hash_t;


/* Initialise a hash table.  */
void string_hash_init (string_hash_t * hash);
/* Free memory owned by a hash table.  */
void string_hash_destroy (string_hash_t * hash);

/* Creates a new bucket for the cached string @c s and returns a pointer to it.
 * pointer to it.  If the bucket already exists, return pointer to that instead.
 * @c *n is set to @c true if a new bucket is created, @c false if an existing
 * bucket is returned.  */
void * string_hash_insert (string_hash_t * hash,
                           const char * s, size_t entry_size, bool * n);
/* String need not be cached.  */
void * string_hash_find (const string_hash_t * hash, const char * string);

/* Start iterating over a hash-table.  */
void * string_hash_begin (const string_hash_t * hash);
/* Next item in hash-table iteration.  */
void * string_hash_next (const string_hash_t * hash, void * i);


#endif

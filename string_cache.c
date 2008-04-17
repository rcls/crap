#include "xmalloc.h"
#include "string_cache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct string_entry_t {
    struct string_entry_t * next;       /* Next in hash chain.  */
    unsigned long hash;                 /* hash.  */
    char data[1];                       /* Actual data.  */
} string_entry_t;


#define HASH_SIZE (1 << 18)
static string_entry_t * hash_table[HASH_SIZE];


const char * cache_string_n (const char * s, size_t len)
{
    assert (memchr (s, 0, len) == NULL);

    unsigned long hash = string_hash (s, len);
    string_entry_t ** bucket = hash_table + hash % HASH_SIZE;
    for (; *bucket; bucket = &(*bucket)->next)
        if ((*bucket)->hash == hash
            && strlen ((*bucket)->data) == len
            && memcmp ((*bucket)->data, s, len) == 0)
            return (*bucket)->data;

    string_entry_t * b = xmalloc (offsetof (string_entry_t, data) + len + 1);
    *bucket = b;
    b->next = NULL;
    b->hash = hash;
    memcpy (b->data, s, len);
    b->data[len] = 0;
    return b->data;
}


const char * cache_string (const char * s)
{
    return cache_string_n (s, strlen (s));
}


unsigned long string_hash_get (const char * s)
{
    return ((string_entry_t *) (s - offsetof (string_entry_t, data)))->hash;
}


unsigned long string_hash (const char * str, size_t len)
{
    unsigned long hash = 0;
    for (int i = 0; i != len; ++i)
        hash = hash * 31 + str[i];
    return hash;
}

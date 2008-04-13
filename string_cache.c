#include "xmalloc.h"
#include "string_cache.h"

#include <string.h>

typedef struct string_entry_t {
    struct string_entry_t * next;       /* Next in hash chain.  */
    unsigned long hash;                 /* hash.  */
    char data[1];                       /* Actual data.  */
} string_entry_t;


#define HASH_SIZE (1 << 18)
static string_entry_t * hash_table[HASH_SIZE];


const char * cache_string (const char * s)
{
    unsigned long hash = string_hash (s);
    string_entry_t ** bucket = hash_table + hash % HASH_SIZE;
    for (; *bucket; bucket = &(*bucket)->next)
        if ((*bucket)->hash == hash && strcmp ((*bucket)->data, s) == 0)
            return (*bucket)->data;

    string_entry_t * b = xmalloc (offsetof (string_entry_t, data)
                                  + strlen (s) + 1);
    *bucket = b;
    b->next = NULL;
    b->hash = hash;
    strcpy (b->data, s);
    return b->data;
}


unsigned long string_hash_get (const char * s)
{
    return ((string_entry_t *) (s - offsetof (string_entry_t, data)))->hash;
}


unsigned long string_hash (const char * str)
{
    unsigned long hash = 0;
    while (*str)
        hash = hash * 31 + (unsigned char) *str++;
    return hash;
}

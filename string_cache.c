#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct string_entry {
    struct string_entry * next;         // Next in hash chain.
    unsigned long hash;                 // hash.
    char data[1];                       // Actual data.
} string_entry_t;

static size_t cache_entries;
static size_t cache_num_buckets;         // Always a power of 2.
static string_entry_t ** cache_table;


static void cache_resize()
{
    if (cache_num_buckets == 0) {
        cache_num_buckets = 1024;        // Start with a reasonable size.
        cache_table = ARRAY_CALLOC (string_entry_t *, cache_num_buckets);
        return;
    }

    cache_table = ARRAY_REALLOC (cache_table, 2 * cache_num_buckets);

    for (size_t i = 0; i != cache_num_buckets; ++i) {
        string_entry_t ** me = cache_table + i;
        string_entry_t ** you = cache_table + cache_num_buckets + i;
        for (string_entry_t * p = *me; p; ) {
            string_entry_t * next = p->next;
            if (p->hash & cache_num_buckets) {
                *you = p;
                you = &p->next;
            }
            else {
                *me = p;
                me = &p->next;
            }
            p = next;
        }
        *me = NULL;
        *you = NULL;
    }

    cache_num_buckets *= 2;
}


const char * cache_string_n (const char * s, size_t len)
{
    assert (memchr (s, 0, len) == NULL);

    unsigned long hash = string_hash_func (s, len);
    string_entry_t ** bucket = cache_table + (hash & (cache_num_buckets - 1));
    if (cache_num_buckets)
        for (; *bucket; bucket = &(*bucket)->next)
            if ((*bucket)->hash == hash
                && strlen ((*bucket)->data) == len
                && memcmp ((*bucket)->data, s, len) == 0)
                return (*bucket)->data;

    if (cache_entries >= cache_num_buckets) {
        cache_resize();
        for (bucket = cache_table + (hash & (cache_num_buckets - 1));
             *bucket; bucket = &(*bucket)->next);
    }

    ++cache_entries;
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


void string_cache_stats (FILE * f)
{
    size_t used = 0;
    unsigned long long sumsq = 0;
    for (size_t i = 0; i != cache_num_buckets; ++i) {
        if (cache_table[i] == NULL)
            continue;

        ++used;
        size_t len = 0;
        for (string_entry_t * p = cache_table[i]; p; p = p->next)
            ++len;

        sumsq += len * (unsigned long long) len;
    }

    fprintf (f, "String cache: %u items, %u/%u buckets used, RMS len %g\n",
             cache_entries, used, cache_num_buckets,
             sqrt (sumsq / (double) used));
}


void string_cache_destroy()
{
    for (size_t i = 0; i != cache_num_buckets; ++i)
        for (string_entry_t * p = cache_table[i]; p; ) {
            string_entry_t * prev = p;
            p = p->next;
            free (prev);
        }
    free (cache_table);
}


unsigned long string_hash_get (const char * s)
{
    return ((string_entry_t *) (s - offsetof (string_entry_t, data)))->hash;
}


unsigned long string_hash_func (const char * str, size_t len)
{
    unsigned long hash = 0;
    for (int i = 0; i != len; ++i)
        hash = hash * 31 + str[i];
    return hash;
}


void string_hash_init (string_hash_t * hash)
{
    hash->num_entries = 0;
    hash->num_buckets = 16;
    hash->buckets = ARRAY_CALLOC (string_hash_head_t *, 16);
}


void string_hash_destroy (string_hash_t * hash)
{
    for (size_t i = 0; i != hash->num_buckets; ++i)
        for (string_hash_head_t * p = hash->buckets[i]; p;) {
            string_hash_head_t * next = p->next;
            free (p);
            p = next;
        }

    free (hash->buckets);
}


static void string_hash_resize (string_hash_t * hash)
{
    hash->buckets = ARRAY_REALLOC (hash->buckets, 2 * hash->num_buckets);

    for (size_t i = 0; i != hash->num_buckets; ++i) {
        string_hash_head_t ** me = hash->buckets + i;
        string_hash_head_t ** you = hash->buckets + hash->num_buckets + i;

        string_hash_head_t * p = *me;

        for (; p; p = p->next)
            if (string_hash_get (p->string) & hash->num_buckets) {
                *you = p;
                you = &p->next;
            }
            else {
                *me = p;
                me = &p->next;
            }

        *me = NULL;
        *you = NULL;
    }

    hash->num_buckets *= 2;
}


static string_hash_head_t ** bucket_find (const string_hash_t * hash,
                                          const char * s, unsigned long sh)
{
    string_hash_head_t ** p = hash->buckets + (sh & (hash->num_buckets - 1));
    for (; *p; p = &(*p)->next)
        if (string_hash_get ((*p)->string) == sh
            && strcmp ((*p)->string, s) == 0)
            break;
    return p;
}


void * string_hash_insert (string_hash_t * hash,
                           const char * s, size_t size, bool * n)
{
    string_hash_head_t ** p = bucket_find (hash, s, string_hash_get (s));
    if (*p) {
        *n = false;
        return *p;
    }

    *n = true;

    if (hash->num_entries >= hash->num_buckets) {
        string_hash_resize (hash);
        p = bucket_find (hash, s, string_hash_get (s));
    }

    string_hash_head_t * pp = xmalloc (size);
    *p = pp;
    pp->string = s;
    pp->next = NULL;
    ++hash->num_entries;
    return pp;
}


void * string_hash_find (const string_hash_t * hash, const char * str)
{
    return *bucket_find (hash, str, string_hash_func (str, strlen (str)));
}


void * string_hash_begin (const string_hash_t * hash)
{
    for (size_t i = 0; i != hash->num_buckets; ++i)
        if (hash->buckets[i])
            return hash->buckets[i];
    return NULL;
}


void * string_hash_next (const string_hash_t * hash, void * i)
{
    string_hash_head_t * ii = i;
    if (ii->next != NULL)
        return ii->next;

    for (size_t j =
             (string_hash_get (ii->string) & (hash->num_buckets - 1)) + 1;
         j != hash->num_buckets; ++j)
        if (hash->buckets[j])
            return hash->buckets[j];

    return NULL;
}

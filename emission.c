#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"

#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>

void version_release (database_t * db, version_t * version)
{
    heap_insert (&db->ready_versions, version);

    assert (version->changeset->unready_count != 0);

    if (--version->changeset->unready_count == 0)
        heap_insert (&db->ready_changesets, version->changeset);
}


void changeset_emitted (database_t * db, changeset_t * changeset)
{
    for (version_t * cs_v = changeset->versions;
         cs_v; cs_v = cs_v->cs_sibling) {
        heap_remove (&db->ready_versions, cs_v);
        for (version_t * v = cs_v->children; v; v = v->sibling)
            version_release (db, v);
    }
}


size_t changeset_update_branch (struct database * db,
                                struct changeset * changeset)
{
    version_t ** branch;
    if (changeset->versions->branch)
        branch = changeset->versions->branch->tag->branch_versions;
    else
        branch = db->trunk_versions;

    size_t changes = 0;
    for (version_t * i = changeset->versions; i; i = i->cs_sibling) {
        version_t * v = i->dead ? NULL : i;
        if (branch[i->file - db->files] != v) {
            branch[i->file - db->files] = v;
            ++changes;
        }
    }

    /* Compute the SHA1 hash of the current branch state.  */
    SHA_CTX sha;
    SHA_Init (&sha);
    version_t ** branch_end = branch + (db->files_end - db->files);
    for (version_t ** i = branch; i != branch_end; ++i)
        if (*i != NULL && !(*i)->dead)
            SHA_Update (&sha, i, sizeof (version_t *));

    uint32_t hash[5];
    SHA1_Final ((unsigned char *) hash, &sha);

    /* Iterate over all the tags that match.  */
    for (tag_t * i = database_tag_hash_find (db, hash); i; i = i->hash_next) {
        printf ("*** HIT %s %s%s ***\n",
                i->branch_versions ? "BRANCH" : "TAG", i->tag,
                i->is_emitted ? " (DUPLICATE)" : "");
        i->is_emitted = true;
    }

    return changes;
}

#ifndef BRANCH_H
#define BRANCH_H

#include <sys/types.h>

typedef struct branch_tag {
    struct tag * tag;
    size_t weight;
} branch_tag_t;


typedef struct parent_branch {
    struct tag * branch;
    size_t weight;
} parent_branch_t;

#endif

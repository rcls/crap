#ifndef LOG_PARSE_H
#define LOG_PARSE_H

#include <stdio.h>
#include <sys/types.h>

struct database;

/// Populate @c database from the given file @c f.  @c l and @c l_len are used
/// for storing lines as they are read fromthe file.
void read_files_versions (struct database * database,
                          char ** restrict l, size_t * l_len, FILE * f);

#endif

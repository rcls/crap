#ifndef LOG_PARSE_H
#define LOG_PARSE_H

#include <stdio.h>
#include <sys/types.h>

struct database;

void read_files_versions (struct database * database,
                          char ** restrict l, size_t * l_len, FILE * f);

#endif

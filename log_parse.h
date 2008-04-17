#ifndef LOG_PARSE_H
#define LOG_PARSE_H

#include <stdio.h>
#include <sys/types.h>

void read_file_version (const char * rcs_file,
                        char ** __restrict__ l, size_t * l_len, FILE * f);
void read_file_versions (char ** __restrict__ l, size_t * l_len, FILE * f);
void read_files_versions (char ** __restrict__ l, size_t * l_len, FILE * f);

#endif

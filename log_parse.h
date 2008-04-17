#ifndef LOG_PARSE_H
#define LOG_PARSE_H

#include <stdio.h>
#include <sys/types.h>

void read_file_versions (char ** __restrict__ l, size_t * buffer_len, FILE * f);

#endif

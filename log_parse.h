#ifndef LOG_PARSE_H
#define LOG_PARSE_H

#include <stdio.h>
#include <sys/types.h>

#include "file.h"

void read_files_versions (char ** restrict l, size_t * l_len, FILE * f);

#endif

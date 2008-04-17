
#include <stdio.h>

#include "log_parse.h"

int main()
{
    char * line = NULL;
    size_t len = 0;

    read_file_versions (&line, &len, stdin);

    return 0;
}


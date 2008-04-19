
#include <stdio.h>

#include "database.h"
#include "log_parse.h"

int main()
{
    char * line = NULL;
    size_t len = 0;

    file_database_t db;

    read_files_versions (&db, &line, &len, stdin);

    return 0;
}


#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <sys/types.h>

typedef struct server_connection {
    FILE * stream;
    const char * remote_root;
    char * line;
    size_t line_len;
} server_connection_t;


void connect_to_server (server_connection_t * conn, const char * root);

void server_connection_destroy (server_connection_t * conn);

/// Call getline and do some sanity checking.
size_t next_line (server_connection_t * s);


#endif

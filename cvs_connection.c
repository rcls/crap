#include "cvs_connection.h"
#include "log.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static const char * pserver_password (const char * root)
{
    size_t root_len = strlen (root);
    const char * home = getenv ("HOME");
    if (home == NULL)
        fatal ("Cannot get home directory");

    const char * path = xasprintf ("%s/.cvspass", home);

    FILE * cvspass = fopen (path, "r");
    xfree (path);
    if (cvspass == NULL)
        return xstrdup ("A");

    char * lineptr = NULL;
    size_t n = 0;

    ssize_t len;
    while ((len = getline (&lineptr, &n, cvspass)) >= 0) {
        if (len > 0 && lineptr[len - 1] == '\n') {
            --len;
            lineptr[len] = 0;
        }
        const char * l = lineptr;
        if (strncmp (l, "/1 ", 3) == 0) {
            l += 3;
            len -= 3;
        }

        if (strncmp (l, root, root_len) == 0 && l[root_len] == ' ') {
            fclose (cvspass);
            size_t plen = strlen (l + root_len + 1);
            memmove (lineptr, l + root_len + 1, plen);
            lineptr[plen] = 0;
            return lineptr;
        }
    }

    fclose (cvspass);
    return xstrdup ("A");
}


static void connect_to_pserver (cvs_connection_t * conn, const char * root)
{
    const char * host = root + strlen (":pserver:");

    const char * path = strchr (host, '/');
    if (path == NULL)
        fatal ("No path in CVS root '%s'\n", root);
    conn->remote_root = path;

    size_t host_len = path - host;

    const char * port = memchr (host, ':', host_len);
    size_t port_len;
    if (port == NULL) {
        port = "2401";
        port_len = 4;
    }
    else {
        host_len = port - host;
        ++port;
        port_len = path - port;
    }

    const char * at = memchr (host, '@', host_len);
    const char * user;
    size_t user_len;
    if (at == NULL) {
        host = root;
        user = getenv ("USER");
        if (user == NULL)
            fatal ("Cannot determine user-name for '%s'\n", root);
        user += 5;
        user_len = strlen (user);
    }
    else {
        user = host;
        user_len = at - host;
        host = at + 1;
        host_len -= user_len + 1;
    }

    host = strndup (host, host_len);
    port = strndup (port, port_len);

    fprintf (stderr, "Pserver '%.*s'@'%s':'%s' '%s'\n",
             user_len, user, host, port, path);

    struct addrinfo hints;
    struct addrinfo * ai;
    memset (&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo (host, port, &hints, &ai);
    if (r != 0)
        fatal ("Could not look-up server %s:%s: %s\n",
               host, port, gai_strerror (r));

    int s = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == -1)
        fatal ("Count not create socket for server %s:%s: %s\n",
               host, port, strerror (errno));

    if (connect (s, ai->ai_addr, ai->ai_addrlen) < 0)
        fatal ("Count not connect to server %s:%s: %s\n",
               host, port, strerror (errno));

    xfree (host);
    xfree (port);
    freeaddrinfo (ai);

    conn->stream = fdopen (s, "rw+");
    if (conn->stream == NULL)
        fatal ("fdopen failed: %s\n", strerror (errno));

    const char * password = pserver_password (root);
    fprintf (stderr, "Password '%s'\n", password);
    cvs_printff (conn,
                "BEGIN AUTH REQUEST\n%s\n%.*s\n%s\nEND AUTH REQUEST\n",
                path, user_len, user, password);
    xfree (password);

    next_line (conn);
    if (strcmp (conn->line, "I LOVE YOU") != 0)
        fatal ("Failed to login: '%s'\n", conn->line);

    fprintf (stderr, "Logged in successfully\n");
}


static FILE * connect_to_program (const char * program,
                                  const char * const argv[])
{
    int sockets[2];
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
        fatal ("socketpair failed: %s\n", strerror (errno));

    int pid = fork();
    if (pid < 0)
        fatal ("fork() failed: %s\n", strerror (errno));

    if (pid != 0) {
        // The parent
        FILE * stream = fdopen (sockets[0], "rw+");
        if (stream == NULL)
            fatal ("fdopen failed: %s\n", strerror (errno));
        close (sockets[1]);
        return stream;
    }

    // The child.
    close (sockets[0]);
    if (dup2 (sockets[1], 0) < 0)
        fatal ("dup2 failed: %s\n", strerror (errno));
    if (dup2 (sockets[1], 1) < 0)
        fatal ("dup2 failed: %s\n", strerror (errno));
    if (sockets[1] > 1)
        close (sockets[1]);

    execvp (program, (char * const *) argv);
    fatal ("exec failed: %s\n", strerror (errno));
}


static void connect_to_fork (cvs_connection_t * conn, const char * path)
{
    static const char * const argv[] = { "cvs", "server", NULL };
    conn->remote_root = path;
    conn->stream = connect_to_program ("cvs", argv);
}


void connect_to_ext (cvs_connection_t * conn,
                     const char * root, const char * path)
{
    const char * program = getenv ("CVS_RSH");
    if (program == NULL)
        program = "ssh";
    else
        program += strlen ("CVS_RSH=");

    conn->remote_root = strchr (path, '/');
    if (conn->remote_root == NULL)
        fatal ("Root '%s' has no remote root.\n", root);
    const char * host = strndup (path, conn->remote_root - path);
    ++conn->remote_root;
    const char * const argv[] = { program, host, "cvs", "server", NULL };
    conn->stream = connect_to_program (program, argv);
    xfree (host);
}


static void connect_to_fake (cvs_connection_t * conn, const char * root)
{
    const char * program = root + strlen (":fake:");
    const char * colon1 = strchr (program, ':');
    if (colon1 == NULL)
        fatal ("Root '%s' has no remote root\n", root);
    const char * colon2 = strchr (colon1 + 1, ':');
    if (colon2 == NULL)
        fatal ("Root '%s' has no remote root\n", root);

    conn->remote_root = colon2 + 1;
    program = strndup (program, colon1 - program);
    const char * argument = strndup (colon1 + 1, colon2 - colon1 - 1);
    const char * const argv[] = { program, argument, NULL };
    conn->stream = connect_to_program (program, argv);
    xfree (program);
    xfree (argument);
}


void connect_to_cvs (cvs_connection_t * conn, const char * root)
{
    conn->count_versions = 0;
    conn->count_transactions = 0;
    conn->log_in = NULL;
    conn->log_out = NULL;
    conn->compress = false;

    const char * client_log = getenv ("CVS_CLIENT_LOG");
    if (client_log) {
//        client_log += strlen ("CVS_CLIENT_LOG=");
        char path[strlen (client_log) + 5];
        strcpy (path, client_log);
        strcat (path, ".in");
        conn->log_in = fopen (path, "w");

        strcpy (path, client_log);
        strcat (path, ".out");
        conn->log_out = fopen (path, "w");
    }

    conn->line = NULL;
    conn->line_len = 0;

    conn->module = NULL;
    conn->prefix = NULL;

    if (starts_with (root, ":pserver:"))
        connect_to_pserver (conn, root);
    else if (starts_with (root, ":fake:"))
        connect_to_fake (conn, root);
    else if (starts_with (root, ":ext:"))
        connect_to_ext (conn, root, root + 5);
    else if (root[0] != '/' && strchr (root, ':') != NULL)
        connect_to_ext (conn, root, root);
    else
        connect_to_fork (conn, root);

    cvs_printff (conn,
                 "Root %s\n"

                 "Valid-responses ok error Valid-requests Checked-in New-entry "
                 "Checksum Copy-file Updated Created Update-existing Merged "
                 "Patched Rcs-diff Mode Removed Remove-entry "
                 // We don't actually want these:
                 // "Set-static-directory Clear-static-directory Set-sticky "
                 // "Clear-sticky Mod-time "
                 "Template Notified Module-expansion "
                 "Wrapper-rcsOption M Mbinary E F MT\n"

                "valid-requests\n"
                "UseUnchanged\n",
                conn->remote_root);

    next_line (conn);
    if (!starts_with (conn->line, "Valid-requests "))
        fatal ("Did not get valid requests ('%s')\n", conn->line);

    fprintf (stderr, "%s\n", conn->line);

    next_line (conn);
    if (strcmp (conn->line, "ok") != 0)
        fatal ("Did not get 'ok'!\n");
}


static size_t next_line_raw (cvs_connection_t * conn)
{
    ssize_t s = getline (&conn->line, &conn->line_len, conn->stream);
    if (s < 0)
        fatal ("Unexpected EOF from server.\n");

    if (strlen (conn->line) < s)
        fatal ("Got line containing ASCII NUL from server.\n");

    if (s > 0 && conn->line[s - 1] == '\n')
        conn->line[--s] = 0;

    if (conn->log_out)
        fprintf (conn->log_out, "%s\n", conn->line);

    return s;
}


size_t next_line (cvs_connection_t * conn)
{
    while (1) {
        ssize_t s = next_line_raw (conn);
        if (conn->line[0] == 'E' && conn->line[1] == ' ')
            fprintf (stderr, "cvs: %s\n", conn->line + 2);
        else if (conn->line[0] == 'F' && conn->line[2] == 0)
            fflush (stderr);
        else
            return s;
    }
}


static void cvs_send (cvs_connection_t * s, const unsigned char * data,
                      size_t length, int flush)
{
    assert (length <= INT_MAX);

    if (!s->compress) {
        // Just write and maybe flush.
        if (fwrite (data, length, 1, s->stream) != 1)
            fatal ("Writing to cvs socket: %s\n", strerror (errno));
        return;
    }

    s->deflater.next_in = (unsigned char *) data;
    s->deflater.avail_in = length;

    int r;
    do {
        unsigned char buffer[4096];
        s->deflater.next_out = buffer;
        s->deflater.avail_out = 4096;

        r = deflate (&s->deflater, flush);
        if (r != Z_OK) {
            assert (r != Z_STREAM_ERROR);
            assert (r != Z_BUF_ERROR);
            assert (r == Z_STREAM_END);
            assert (flush == Z_FINISH);
        }

        int done = 4096 - s->deflater.avail_out;
        if (done != 0 && fwrite (buffer, done, 1, s->stream) != 1)
            fatal ("Writing to cvs socket: %s\n", strerror (errno));
    }
    while (s->deflater.avail_in != 0
           || (flush == Z_FINISH && r == Z_OK)
           || (flush != Z_NO_FLUSH && s->deflater.avail_out == 0));
}


static void cvs_do_printf (cvs_connection_t * s, int flush,
                           const char * format, va_list args)
{
    if (s->log_in) {
        va_list copy;
        va_copy (copy, args);
        vfprintf (s->log_in, format, args); // Ignore errors.
        va_end (copy);
    }

    char * string;
    int len = vasprintf (&string, format, args);
    if (len < 0)
        fatal ("Failed to format a string; %s\n", strerror (errno));

    cvs_send (s, (const unsigned char *) string, len, flush);
    free (string);
}


void cvs_printf (cvs_connection_t * s, const char * format, ...)
{
    va_list args;
    va_start (args, format);
    cvs_do_printf (s, Z_NO_FLUSH, format, args);
    va_end (args);
}


void cvs_printff (cvs_connection_t * s, const char * format, ...)
{
    va_list args;
    va_start (args, format);
    cvs_do_printf (s, Z_SYNC_FLUSH, format, args);
    va_end (args);
    if (fflush (s->stream) != 0)
        fatal ("Writing to cvs socket: %s\n", strerror (errno));
}


void cvs_connection_destroy (cvs_connection_t * s)
{
    xfree (s->line);
    xfree (s->module);
    xfree (s->prefix);

    fclose (s->stream);
    if (s->log_in)
        fclose (s->log_in);
    if (s->log_out)
        fclose (s->log_out);

    if (s->compress) {
        deflateEnd (&s->deflater);
        deflateEnd (&s->inflater);
    }
}


static const char * file_error (FILE * f)
{
    return ferror (f) ? strerror (errno) : (feof (f) ? "EOF" : "unknown");
}


void cvs_read_block (cvs_connection_t * s, FILE * f, size_t bytes)
{
    for (size_t done = 0; done != bytes; ) {
        char buffer[4096];
        size_t get = bytes - done;
        if (get > 4096)
            get = 4096;
        size_t got = fread (&buffer, 1, get, s->stream);
        if (got == 0)
            fatal ("cvs checkout: %s\n", file_error (s->stream));
        if (f && fwrite (&buffer, got, 1, f) != 1)
            fatal ("git import interrupted: %s\n", file_error (f));

        done += got;
    }

    if (s->log_out)
        fprintf (s->log_out, "[%zu bytes of data]\n", bytes);
}


void cvs_connection_compress (cvs_connection_t * s, int level)
{
    if (s->compress || level == 0)
        return;                         // Nothing to do.

    s->deflater.zalloc = Z_NULL;
    s->deflater.zfree = Z_NULL;
    s->deflater.opaque = Z_NULL;
    if (deflateInit (&s->deflater, level) != Z_OK)
        fatal ("failed to initialise compression\n");

    s->inflater.zalloc = Z_NULL;
    s->inflater.zfree = Z_NULL;
    s->inflater.opaque = Z_NULL;
    s->inflater.next_in = Z_NULL;
    s->inflater.avail_in = 0;
    if (inflateInit (&s->inflater) != Z_OK)
        fatal ("failed to initialise compression\n");

    s->compress = true;
}

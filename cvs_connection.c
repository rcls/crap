#include "cvs_connection.h"
#include "log.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pipeline.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#define EMPTY_PASSWORD ""
#define FALLBACK_PASSWORD EMPTY_PASSWORD

static inline unsigned char * in_max (cvs_connection_t * s)
{
    return s->in + sizeof s->in;
}


static inline unsigned char * out_max (cvs_connection_t * s)
{
    return s->out + sizeof s->out;
}


static const char * lookup_password_in_cvspass_file (const char * pserver_root)
{
    fprintf (stderr, "Looking up password in $HOME/.cvspass...\n");
    size_t root_len = strlen (pserver_root);
    const char * home = getenv ("HOME");
    if (home == NULL)
        fatal ("Cannot get home directory");

    const char * path = xasprintf ("%s/.cvspass", home);

    FILE * cvspass_file = fopen (path, "r");
    xfree (path);
    if (cvspass_file == NULL)
        return NULL;

    char * lineptr = NULL;
    size_t n = 0;

    ssize_t len;
    while ((len = getline (&lineptr, &n, cvspass_file)) >= 0) {
        if (len > 0 && lineptr[len - 1] == '\n') {
            --len;
            lineptr[len] = 0;
        }
        const char * l = lineptr;
        if (strncmp (l, "/1 ", 3) == 0) {
            l += 3;
            len -= 3;
        }

        if (strncmp (l, pserver_root, root_len) == 0 && l[root_len] == ' ') {
            fclose (cvspass_file);
            size_t plen = strlen (l + root_len + 1);
            memmove (lineptr, l + root_len + 1, plen);
            lineptr[plen] = 0;
            return lineptr;
        }
    }

    fclose (cvspass_file);
    return NULL;
}


static void connect_to_pserver (cvs_connection_t * conn, const char * root)
{
    const char * host = root + strlen (":pserver:");

    const char * path = strchr (host, '/');
    if (path == NULL)
        fatal ("No path in CVS root '%s'\n", root);
    conn->remote_root = xstrdup(path);

    size_t host_len = path - host;

    const char * port = memchr (host, ':', host_len);
    size_t port_len = 0;
    if (port != NULL) {
        host_len = port - host;
        ++port;
        port_len = path - port;
    }
    if (port_len == 0) {
        port = "2401";
        port_len = 4;
    }

    const char * at = memchr (host, '@', host_len);
    const char * user;
    size_t user_len;
    if (at == NULL) {
        user = getenv ("USER");
        if (user == NULL)
            fatal ("Cannot determine user-name for '%s'\n", root);
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
             (int) user_len, user, host, port, path);

    struct addrinfo hints;
    struct addrinfo * ai;
    memset (&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo (host, port, &hints, &ai);
    if (r != 0)
        fatal ("Could not look-up server %s:%s: %s\n",
               host, port, gai_strerror (r));

    conn->socket = check (
        socket (ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol),
        "socket for server %s:%s", host, port);

    check (connect (conn->socket, ai->ai_addr, ai->ai_addrlen),
           "Connect to server %s:%s", host, port);

    xfree (host);
    xfree (port);
    freeaddrinfo (ai);

    if (conn->password == NULL) {
        conn->password = lookup_password_in_cvspass_file (root);
        if (conn->password == NULL) {
            conn->password = FALLBACK_PASSWORD;
            fprintf (stderr, "No password supplied and none found; will try an empty password.\n");
        }
        else {
            conn->password_is_malloced = true;
        }
    }
    fprintf (stderr, "Password '%s'\n", conn->password);
    cvs_printff (conn,
                "BEGIN AUTH REQUEST\n%s\n%.*s\n%s\nEND AUTH REQUEST\n",
                 path, (int) user_len, user, conn->password);
    if (conn->password_is_malloced) {
        xfree (conn->password);
        conn->password_is_malloced = false;
    }

    next_line (conn);
    if (strcmp (conn->line, "I LOVE YOU") != 0)
        fatal ("Failed to login: '%s'\n", conn->line);

    fprintf (stderr, "Logged in successfully\n");
}


static void connect_to_program (cvs_connection_t * restrict conn,
                                const char * name, ...) PIPELINE_ATTR_SENTINEL;
static void connect_to_program (cvs_connection_t * restrict conn,
                                const char * name, ...)
{
    int sockets[2];
    check (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets),
           "socketpair failed");

    // libpipeline doesn't cope with a FD being used twice.  So dup it.
    int sdup = check (open ("/dev/null", O_RDONLY|O_CLOEXEC), "open /dev/null");
    check (dup3 (sockets[1], sdup, O_CLOEXEC), "dup3");

    va_list argv;
    va_start (argv, name);
    conn->pipeline = pipeline_new_command_argv (name, argv);
    va_end (argv);
    pipeline_want_in (conn->pipeline, sockets[1]);
    pipeline_want_out (conn->pipeline, sdup);
    pipeline_start (conn->pipeline);

    conn->socket = sockets[0];
}


static void connect_to_fork (cvs_connection_t * conn, const char * path)
{
    if (path[0] == '/') {
        conn->remote_root = xstrdup(path);
    }
    else {
        const char * cwd = getcwd(NULL, 0);
        if (cwd == NULL)
            fatal("getcwd() failed: %m");
        const char * sep = "";
        if (path[0] && !ends_with(cwd, "/"))
            sep = "/";
        conn->remote_root = xasprintf("%s%s%s", cwd, sep, path);
        xfree(cwd);
    }
    connect_to_program (conn, "cvs", "server", NULL);
}


void connect_to_ext (cvs_connection_t * conn,
                     const char * root, const char * path)
{
    const char * program = getenv ("CVS_RSH");
    if (program == NULL)
        program = "ssh";

    // Split into host and directory as follows:  Find the first ':' or '/'.  A
    // ':' separates the host and directory, a '/' starts the directory.
    const char * sep = path + strcspn (path, ":/");
    if (*sep == '\0')
        fatal ("Root '%s' has no remote root.\n", root);

    if (*sep == ':')
        conn->remote_root = xstrdup(sep + 1);
    else
        conn->remote_root = xstrdup(sep);

    const char * host = strndup (path, sep - path);
    connect_to_program (conn, program, host, "cvs", "server", NULL);
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

    conn->remote_root = xstrdup(colon2 + 1);
    program = strndup (program, colon1 - program);
    const char * argument = strndup (colon1 + 1, colon2 - colon1 - 1);
    connect_to_program (conn, program, argument, NULL);
    xfree (program);
    xfree (argument);
}


void connect_to_cvs (cvs_connection_t * conn, const char * root)
{
    conn->count_versions = 0;
    conn->count_transactions = 0;
    conn->log = NULL;
    conn->pipeline = NULL;
    conn->compress = false;

    const char * client_log = getenv ("CVS_CLIENT_LOG");
    if (client_log)
        conn->log = fopen (client_log, "we");

    conn->in_next = conn->in;
    conn->in_end = conn->in;
    conn->out_next = conn->out;

    conn->module = NULL;
    conn->prefix = NULL;
    conn->remote_root = NULL;

    if (starts_with (root, ":pserver:"))
        connect_to_pserver (conn, root);
    else if (starts_with (root, ":fake:"))
        connect_to_fake (conn, root);
    else if (starts_with (root, ":ext:"))
        connect_to_ext (conn, root, root + 5);
    else if (starts_with(root, ":local:"))
        connect_to_fork(conn, root + 7);
    else if (starts_with(root, ":fork:"))
        connect_to_fork(conn, root + 6);
    else if (starts_with(root, "./"))
        // CVS would try and interpret ./foo as host "." and path "/foo".  But
        // "." has no A or AAAA DNS records, so it is safe to take this as a
        // relative path.
        connect_to_fork(conn, root);
    else if (root[0] != '/' && root[strcspn(root, "/:")])
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


static const char * file_error (FILE * f)
{
    return ferror (f) ? strerror (errno) : (feof (f) ? "EOF" : "unknown");
}


static size_t checked_read (cvs_connection_t * s, void * buf, size_t count)
{
    size_t r = check (read (s->socket, buf, count), "Reading from CVS server");
    if (r == 0)
        fatal ("Unexpected EOF from CVS server.\n");
    return r;
}


static void do_read (cvs_connection_t * s)
{
    if (s->in_end == in_max (s)) {
        // Shuffle data.
        assert (s->in_next != s->in);
        size_t bytes = s->in_end - s->in_next;
        memmove (s->in, s->in_next, bytes);
        s->in_next = s->in;
        s->in_end = s->in + bytes;
    }
    if (!s->compress) {
        s->in_end += checked_read (s, s->in_end, in_max (s) - s->in_end);
        return;
    }

    s->inflater.next_out = s->in_end;
    s->inflater.avail_out = in_max (s) - s->in_end;
    while (1) {
        // Unfortunately, we can't just look at avail_in and avail_out to tell
        // if we need to read from the socket, because some data might be
        // buffered within zlib.  So we do a trial inflate() and see what
        // happens.
        int r = inflate (&s->inflater, Z_SYNC_FLUSH);
        if (r == Z_MEM_ERROR)
            fatal ("Out-of-memory decompressing data from CVS");

        if (s->in_end != s->inflater.next_out) {
            s->in_end = s->inflater.next_out;
            return;
        }

        assert (s->inflater.avail_out != 0);
        assert (s->inflater.avail_in == 0);
        s->inflater.avail_in = checked_read (s, s->zin, sizeof s->zin);
        s->inflater.next_in = s->zin;
    }
}


static size_t next_line_raw (cvs_connection_t * s)
{
    char * nl;
    while (s->in_end == s->in_next
           || (nl = memchr (s->in_next, '\n',
                            s->in_end - s->in_next)) == NULL) {
        if (s->in_end == in_max (s) && s->in_next == s->in)
            fatal ("Line from CVS server is too long.\n");
        do_read (s);
    }

    *nl = 0;
    s->line = (char *) s->in_next;
    s->in_next = (unsigned char *) nl + 1;
    return nl - s->line;
}


size_t next_line (cvs_connection_t * s)
{
    while (1) {
        ssize_t len = next_line_raw (s);
        if (s->log)
            fprintf (s->log, " %s\n", s->line);
        if (s->line[0] == 'E' && s->line[1] == ' ')
            fprintf (stderr, "cvs: %s\n", s->line + 2);
        else if (s->line[0] == 'F' && s->line[1] == 0)
            fflush (stderr);
        else
            return len;
    }
}


static void do_write (cvs_connection_t * s,
                      const unsigned char * data, size_t length)
{
    while (length) {
        ssize_t r = check (write (s->socket, data, length),
                           "Write to CVS server");
        if (r == 0)
            fatal ("Huh?  Write to CVS returns 0\n");
        data += r;
        length -= r;
    }
}



static void cvs_send (cvs_connection_t * s, const unsigned char * data,
                      size_t length, int flush)
{
    assert (length <= INT_MAX);

    if (!s->compress) {
        if (length > (size_t) (out_max (s) - s->out_next)) {
            // Flush current data.
            do_write (s, s->out, s->out_next - s->out);
            s->out_next = s->out;
        }
        if (length > (size_t) (out_max (s) - s->out_next))
            // Do big writes immediately.
            do_write (s, data, length);
        else {
            memcpy (s->out_next, data, length);
            s->out_next += length;
        }
        return;
    }

    s->deflater.next_in = (unsigned char *) data; // Zlib isn't const-correct.
    s->deflater.avail_in = length;

    int r;
    do {
        if (s->out_next == out_max (s)) {
            // Buffer is full; flush.
            do_write (s, s->out, s->out_next - s->out);
            s->out_next = s->out;
        }

        s->deflater.next_out = s->out_next;
        s->deflater.avail_out = out_max (s) - s->out_next;

        r = deflate (&s->deflater, flush);
        if (r != Z_OK) {
            assert (r != Z_STREAM_ERROR);
            assert (r != Z_BUF_ERROR);
            assert (r == Z_STREAM_END);
            assert (flush == Z_FINISH);
        }
        s->out_next = s->deflater.next_out;
    }
    while (s->deflater.avail_out == 0
           || s->deflater.avail_in != 0
           || (flush == Z_FINISH && r == Z_OK));
}


static void cvs_do_printf (cvs_connection_t * s, int flush,
                           const char * format, va_list args)
{
    if (s->log) {
        va_list copy;
        va_copy (copy, args);
        vfprintf (s->log, format, copy); // Ignore errors.
        va_end (copy);
    }

    // FIXME - this takes an extra copy of the data.
    char * string;
    int len = check (vasprintf (&string, format, args), "Formatting string");

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

    do_write (s, s->out, s->out_next - s->out);
    s->out_next = s->out;
}


void cvs_connection_destroy (cvs_connection_t * s)
{
    xfree (s->module);
    xfree (s->prefix);
    xfree (s->remote_root);

    close (s->socket);
    if (s->log)
        fclose (s->log);

    if (s->pipeline != NULL) {
        int code = pipeline_wait (s->pipeline);
        if (code != 0)
            fatal ("CVS connection exit status is non-zero: %i\n", code);

        pipeline_free (s->pipeline);
    }

    if (s->compress) {
        deflateEnd (&s->deflater);
        inflateEnd (&s->inflater);
    }
}


void cvs_read_block (cvs_connection_t * s, FILE * f, size_t bytes)
{
    size_t done = 0;
    while (1) {
        size_t avail = s->in_end - s->in_next;
        if (avail > bytes - done)
            avail = bytes - done;

        if (avail != 0 && f != NULL && fwrite (s->in_next, avail, 1, f) != 1)
            fatal ("git import interrupted [%zu %u]: %s\n",
                   avail, 1, file_error (f));

        done += avail;
        s->in_next += avail;
        if (s->in_next == s->in_end) {
            s->in_next = s->in;
            s->in_end = s->in;
        }

        if (done == bytes)
            break;

        do_read (s);
    }

    if (s->log)
        fprintf (s->log, "[%zu bytes of data]\n", bytes);
}


void cvs_connection_compress (cvs_connection_t * s, int level)
{
    if (s->compress || level == 0)
        return;                         // Nothing to do.

    cvs_printff (s, "Gzip-stream %d\n", level);

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

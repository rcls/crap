#include "log.h"
#include "utils.h"

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


static const char * pserver_password (const char * root)
{
    size_t root_len = strlen (root);
    const char * home = getenv ("HOME");
    if (home == NULL)
        fatal ("Cannot get home directory");

    char * path;
    if (asprintf (&path, "%s/.cvspass", home) < 0)
        fatal ("Huh? %s\n", strerror (errno));
    FILE * cvspass = fopen (path, "r");
    xfree (path);
    if (cvspass == NULL)
        return strdup ("A");

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
    return strdup ("A");
}


static FILE * connect_to_pserver (const char * root,
                                  const char ** rroot)
{
    const char * host = root + strlen (":pserver:");

    const char * path = strchr (host, '/');
    if (path == NULL)
        fatal ("No path in CVS root '%s'\n", root);
    *rroot = path;

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
    memset (&hints, 0, sizeof (hints));
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

    FILE * stream = fdopen (s, "rw+");
    if (stream == NULL)
        fatal ("fdopen failed: %s\n", strerror (errno));

    const char * password = pserver_password (root);
    fprintf (stderr, "Password '%s'\n", password);
    if (fprintf (stream, "BEGIN AUTH REQUEST\n%s\n%.*s\n%s\nEND AUTH REQUEST\n",
                 path, user_len, user, password) < 0)
        fatal ("Writing to socket: %s\n", strerror (errno));
    xfree (password);

    size_t n = 0;
    char * lineptr = NULL;
    next_line (&lineptr, &n, stream);
    if (strcmp (lineptr, "I LOVE YOU") != 0)
        fatal ("Failed to login: '%s'\n", lineptr);

    fprintf (stderr, "Logged in successfully\n");

    xfree (lineptr);

    return stream;
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


static FILE * connect_to_fork (const char * path, const char ** rroot)
{
    static const char * const argv[] = { "cvs", "server", NULL };
    *rroot = path;
    return connect_to_program ("cvs", argv);
}


static FILE * connect_to_ext (const char * root, const char * path,
                              const char ** rroot)
{
    const char * program = getenv ("CVS_RSH");
    if (program == NULL)
        program = "ssh";
    else
        program += strlen ("CVS_RSH=");

    *rroot = strchr (path, '/');
    if (*rroot == NULL)
        fatal ("Root '%s' has no remote root.\n", root);
    const char * host = strndup (path, *rroot - path);
    ++*rroot;
    const char * const argv[] = { program, host, "cvs", "server", NULL };
    FILE * stream = connect_to_program (program, argv);
    xfree (host);
    return stream;
}


static FILE * connect_to_fake (const char * root, const char ** rroot)
{
    const char * program = root + strlen (":fake:");
    const char * colon1 = strchr (program, ':');
    if (colon1 == NULL)
        fatal ("Root '%s' has no remote root\n", root);
    const char * colon2 = strchr (colon1 + 1, ':');
    if (colon2 == NULL)
        fatal ("Root '%s' has no remote root\n", root);

    *rroot = colon2 + 1;
    program = strndup (program, colon1 - program);
    const char * argument = strndup (colon1 + 1, colon2 - colon1 - 1);
    const char * const argv[] = { program, argument, NULL };
    FILE * stream = connect_to_program (program, argv);
    xfree (program);
    xfree (argument);
    return stream;
}


FILE * connect_to_server (const char * root)
{
    FILE * stream;
    const char * rroot;
    if (starts_with (root, ":pserver:"))
        stream = connect_to_pserver (root, &rroot);
    else if (starts_with (root, ":fake:"))
        stream = connect_to_fake (root, &rroot);
    else if (starts_with (root, ":ext:"))
        stream = connect_to_ext (root, root + 5, &rroot);
    else if (root[0] != '/' && strchr (root, ':') != NULL)
        stream = connect_to_ext (root, root, &rroot);
    else
        stream = connect_to_fork (root, &rroot);

    fprintf (stream,
             "Root %s\n"

             "Valid-responses ok error Valid-requests Checked-in New-entry "
             "Checksum Copy-file Updated Created Update-existing Merged "
             "Patched Rcs-diff Mode Mod-time Removed Remove-entry "
             "Set-static-directory Clear-static-directory Set-sticky "
             "Clear-sticky Template Notified Module-expansion "
             "Wrapper-rcsOption M Mbinary E F MT\n"

             "valid-requests\n"
             "UseUnchanged\n",
             rroot);

    size_t len = 0;
    char * line = NULL;
    next_line (&line, &len, stream);
    if (!starts_with (line, "Valid-requests "))
        fatal ("Did not get valid requests ('%s')\n", line);

    fprintf (stderr, "%s\n", line);

    next_line (&line, &len, stream);
    if (strcmp (line, "ok") != 0)
        fatal ("Did not get 'ok'!\n");

    xfree (line);

    return stream;
}

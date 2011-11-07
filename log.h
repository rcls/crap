#ifndef LOG_H
#define LOG_H

#include <unistd.h>

void fatal (const char * f, ...)
    __attribute__ ((format (printf, 1, 2), noreturn));

void warning (const char * f, ...) __attribute__ ((format (printf, 1, 2)));

ssize_t check (ssize_t val, const char * fmt, ...)
    __attribute__ ((format (printf, 2, 3)));

#endif

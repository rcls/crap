#ifndef LOG_H
#define LOG_H

void fatal (const char * f, ...)
    __attribute__ ((format (printf, 1, 2), noreturn));

void warning (const char * f, ...) __attribute__ ((format (printf, 1, 2)));

#endif

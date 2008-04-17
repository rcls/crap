#ifndef LOG_H
#define LOG_H

void bugger (const char * f, ...)
    __attribute__ ((format (printf, 1, 2), noreturn));

#endif

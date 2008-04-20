#ifndef UTILS_H
#define UTILS_H

#define ARRAY_EXTEND(P,S,M) do { if (++S > M) { \
            M = M ? M + M/2 : 8;                \
            P = xrealloc (P, M * sizeof (*P));  \
        } } while (0);

#endif

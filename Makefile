
CRAP_OBJS=xmalloc.o string_cache.o

all: $(CRAP_OBJS)

CFLAGS=-O2 -Wall -Werror -std=gnu99

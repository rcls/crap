
CRAP_OBJS=log.o log_parse.o string_cache.o xmalloc.o

LD=gcc

all: rlog_parse

rlog_parse: rlog_parse.o libcrap.a
	$(LD) $(LDFLAGS) -o $@ $+

libcrap.a: $(CRAP_OBJS)
	ar crv $@ $+

CFLAGS=-O2 -Wall -Werror -std=gnu99 -D_GNU_SOURCE -g3

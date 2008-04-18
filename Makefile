

LD=gcc

all: rlog_parse

rlog_parse: rlog_parse.o libcrap.a
	$(LD) $(LDFLAGS) -o $@ $+

libcrap.a: file.o log.o log_parse.o string_cache.o xmalloc.o
	ar crv $@ $+

CFLAGS=-O2 -Wall -Werror -std=gnu99 -D_GNU_SOURCE -g3 \
	-MMD -MP -MF.deps/$(subst /,:,$@).d
CC=gcc

%.o: %.c
	@mkdir -p .deps
	$(COMPILE.c) -o $@ -c $<

.PHONY: all clean
clean:
	rm *.a *.o

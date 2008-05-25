
LD=gcc

all: crap-clone rlog_parse

%: %.o
	$(LD) $(LDFLAGS) -o $@ $+

%: %.c

crap-clone: libcrap.a -lm
rlog_parse: libcrap.a -lm

libcrap.a: branch.o changeset.o cvs_connection.o database.o emission.o file.o \
	heap.o log.o log_parse.o string_cache.o utils.o
	ar crv $@ $+

CFLAGS=-O2 -Wall -Werror -std=gnu99 -D_GNU_SOURCE -g3 \
	-MMD -MP -MF.deps/$(subst /,:,$@).d
CC=gcc

%.o: %.c
	@mkdir -p .deps
	$(COMPILE.c) -o $@ -c $<

.PHONY: all clean
clean:
	rm -f *.a *.o core.* vgcore.*

-include .deps/*.d

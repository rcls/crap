
LD=$(CC)

all: crap-clone rlog_parse

%: %.o
	$(LD) $(LDFLAGS) -o $@ $+ $($*_LIBS)

%: %.c

crap-clone: libcrap.a
crap-clone_LIBS=-lpipeline -lz -lm
rlog_parse: libcrap.a
rlog_parse_LIBS=-lpipeline -lz -lm

libcrap.a: branch.o changeset.o cvs_connection.o database.o emission.o file.o \
	fixup.o heap.o log.o log_parse.o string_cache.o utils.o
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

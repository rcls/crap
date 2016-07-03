
LD=$(CC)

all: crap-clone

%: %.o
	$(LD) $(LDFLAGS) -o $@ $+ $($*_LIBS)

%: %.c

crap-clone: libcrap.a
crap-clone_LIBS=-lpipeline -lz -lm

libcrap.a: branch.o changeset.o cvs_connection.o database.o emission.o file.o \
	filter.o fixup.o heap.o log.o log_parse.o string_cache.o utils.o
	ar crv $@ $+

# For old versions of gcc, you might need to add -std=c99 -fms-extensions.
# or " -Wno-pointer-arith -fms-extensions -pedantic -Wno-format "
CFLAGS=-O2 -std=c99 -Wall -Wextra -Werror -D_GNU_SOURCE -g3 \
	-MMD -MP -MF.deps/$(subst /,:,$@).d
CC=gcc

%.o: %.c
	@mkdir -p .deps
	$(COMPILE.c) -o $@ -c $<

%.s: %.c
	@mkdir -p .deps
	$(COMPILE.c:-g3=) -o $@ -S $<

.PHONY: all clean
clean:
	rm -f *.a *.o core.* vgcore.*

-include .deps/*.d

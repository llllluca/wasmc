#Special target to get portability and reliable POSIX behavior.
.POSIX:
#The .SUFFIXES special target controls the list of file extensions used in
#suffix rules, which are Make’s legacy pattern-matching system. The first
#empty .SUFFIXES clears the suffix list.
.SUFFIXES:

QBE_HEADER = \
	src/qbe-1.2/all.h \
	src/qbe-1.2/ops.h

QBE_OBJ = \
	src/qbe-1.2/util.o \
	src/qbe-1.2/abi.o \
	src/qbe-1.2/cfg.o \
	src/qbe-1.2/mem.o \
	src/qbe-1.2/ssa.o \
	src/qbe-1.2/alias.o \
	src/qbe-1.2/load.o \
    src/qbe-1.2/copy.o \
	src/qbe-1.2/fold.o \
	src/qbe-1.2/simpl.o \
	src/qbe-1.2/live.o \
	src/qbe-1.2/spill.o \
	src/qbe-1.2/rega.o \
	src/qbe-1.2/emit.o

QBE_AMD64_OBJ = \
	src/qbe-1.2/amd64/targ.o \
	src/qbe-1.2/amd64/sysv.o \
	src/qbe-1.2/amd64/isel.o \
	src/qbe-1.2/amd64/emit.o

LIBQBE_OBJ = \
	src/libqbe.o

OBJ = \
	src/parse.o \
	src/compile.o \
	src/dalist.o \
	src/utils.o \
	src/main.o

ALL_OBJ = \
	$(OBJ) \
	$(LIBQBE_OBJ) \
	$(QBE_OBJ) \
	$(QBE_AMD64_OBJ)

CC     = cc
CFLAGS = -std=c99 -g -Wall -Wextra -Wpedantic -Isrc

all: wasmc

wasmc: $(ALL_OBJ)
	$(CC) $(LDFLAGS) $(ALL_OBJ) -o $@

#Add .c and .o to the suffix list.
.SUFFIXES: .o .c
#After setting .SUFFIXES, the suffix rule .c.o: is a template that build an
#object file from a C source file.
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

#Each object target depends on these header files.
$(OBJ): src/all.h src/dalist.h src/libqbe.h
$(LIBQBE_OBJ): $(QBE_HEADER) src/libqbe.h
$(QBE_OBJ) $(QBE_AMD64_OBJ): $(QBE_HEADER)
$(QBE_AMD64_OBJ): src/qbe-1.2/amd64/all.h

clean:
	rm -f $(ALL_OBJ) wasmc

#A phony target is simply a target that is always out-of-date, so whenever
#you run make <phony_target>, it will run, independent from the state of the
#file system.
.PHONY: all clean

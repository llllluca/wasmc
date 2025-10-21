#Special target to get portability and reliable POSIX behavior.
.POSIX:
#The .SUFFIXES special target controls the list of file extensions used in
#suffix rules, which are Make’s legacy pattern-matching system. The first
#empty .SUFFIXES clears the suffix list.
.SUFFIXES:

LIBQBE_OBJ = \
	src/libqbe.o

OBJ = \
	src/parse.o \
	src/compile.o \
	src/adlist.o \
	src/utils.o \
	src/main.o \
	src/ssa.o

ALL_OBJ = \
	$(OBJ) \
	$(LIBQBE_OBJ) \

CC     = cc
CFLAGS = -std=gnu23 -g -Wall -Wextra -Wpedantic -Isrc

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
$(OBJ): src/all.h src/adlist.h src/libqbe.h
$(LIBQBE_OBJ): $(QBE_HEADER) src/libqbe.h

clean:
	rm -f $(ALL_OBJ) wasmc

#A phony target is simply a target that is always out-of-date, so whenever
#you run make <phony_target>, it will run, independent from the state of the
#file system.
.PHONY: all clean

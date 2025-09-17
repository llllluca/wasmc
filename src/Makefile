OBJ = main.o parse.o compile.o utils.o dalist.o
CFLAGS = -std=c99 -g -Wall -Wextra -Wpedantic

main: $(OBJ)
	gcc $(CFLAGS) -o $@ $(OBJ)

%.o: %.c all.h dalist.h
	gcc $(CFLAGS) -c $<

clean:
	rm -f main *.o

.PHONY: clean

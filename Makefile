CC = gcc
CFlags = -Wall -W -pedantic -std=c99

all: old-kilo kilo

kilo: kilo.c
	$(CC) kilo.c $(CFlags) -o kilo

old-kilo: old-kilo.c
	$(CC) old-kilo.c $(CFlags) -o old-kilo

format:
	clang-format kilo.c

clean:
	rm kilo

.PHONY: all clean

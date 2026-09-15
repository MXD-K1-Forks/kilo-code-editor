CC = gcc
CFlags = -Wall -W -pedantic -std=c99
DebugFlags = -fsanitize=address -fno-omit-frame-pointer -g -O0

all: old-kilo kilo

kilo: kilo.c
	$(CC) kilo.c $(CFlags) -o kilo

debug-kilo: kilo.c
	$(CC) kilo.c $(CFlags) $(DebugFlags) -o kilo

old-kilo: old-kilo.c
	$(CC) old-kilo.c $(CFlags) -o old-kilo

format:
	clang-format kilo.c

clean:
	rm kilo

.PHONY: all clean

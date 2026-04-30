CC = gcc
CFLAGS = -Wall -Wextra -pedantic

.PHONY: all clean run

all: shocker prompter

shocker: shocker.c shockerfile.c
	$(CC) $(CFLAGS) -o $@ $^

prompter: prompter.c
	$(CC) $(CFLAGS) -o $@ $<

run: all
	./shocker

clean:
	rm -f shocker prompter

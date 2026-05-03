CC = gcc
CFLAGS = -Wall -Wextra -pedantic

.PHONY: all clean run

all: shocker prompter

shocker: shocker.c shockerfile.c signals.c env_manager.c pkg_adapter.c
	$(CC) $(CFLAGS) -o $@ $^

prompter: prompter.c pkg_adapter.c proc_manager.c env_manager.c shockerfile.c
	$(CC) $(CFLAGS) -o $@ $^

run: all
	./shocker

clean:
	rm -f shocker prompter
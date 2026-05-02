#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include "signals.h"
#include <errno.h>
#include "env_manager.h"
#include "shockerfile.h"
#include <signal.h>

#define PROMPTER_PATH "./prompter"
#define MAX_PROMPT_LEN 1024
#define MAX_ARGS 64
#define HISTORY_SIZE 100

#define KEY_BACKSPACE 127
#define KEY_ESCAPE 27

// environment variable for prompter to use as base directory for environments
extern char **environ;

static char history[HISTORY_SIZE][MAX_PROMPT_LEN];
static int history_count = 0;

static const char *COMMAND_NAMES[] = {
    "help", "exit", "quit",
    "install", "remove", "create", "destroy"
};

/* FIXED: replaced variable-length array usage */
#define MAX_COMMANDS 10

static void add_history(const char *line) {
    if (!line || !line[0]) return;

	// check for consecutive duplicates
    if (history_count > 0 &&
        strcmp(history[(history_count - 1) % HISTORY_SIZE], line) == 0)
        return;

    strncpy(history[history_count % HISTORY_SIZE], line, MAX_PROMPT_LEN - 1);
    history[history_count % HISTORY_SIZE][MAX_PROMPT_LEN - 1] = '\0';
    history_count++;
}

static int get_history_count(void) {
	// ternary operator to return the correct count without exceeding HISTORY_SIZE
    return history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
}

static const char *get_history_entry(int index) {
	// gets history entry starting from the last HISTORY_SIZE (100) entries.
    int count = get_history_count();
    int start = history_count - count;
    return history[(start + index) % HISTORY_SIZE];
}

static void refresh_prompt_line(const char *buffer) {
    printf("\rShocker> %s", buffer);
    printf("\033[K");
    fflush(stdout);
}

static int enable_raw_mode(struct termios *orig) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, orig) == -1) return -1;

    raw = *orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode(const struct termios *orig) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
}

static void handle_tab(char *buffer, size_t *len) {
    int matches[MAX_COMMANDS];
    int count = 0;

    int prefix_len = 0;
    while (buffer[prefix_len] && buffer[prefix_len] != ' ')
        prefix_len++;

    for (int i = 0; i < 7; i++) {
        if (strncmp(COMMAND_NAMES[i], buffer, prefix_len) == 0)
            matches[count++] = i;
    }

    if (count == 1) {
        const char *match = COMMAND_NAMES[matches[0]];
        strcpy(buffer, match);
        buffer[strlen(match)] = ' ';
        buffer[strlen(match) + 1] = '\0';
        *len = strlen(buffer);
        refresh_prompt_line(buffer);
    }
}

/// @brief Reads user input with support for line editing, history navigation, and tab completion.
/// @param buffer The current command line being typed/edited.
/// @param size The total capacity of buffer. 
/// @return 
static int read_input(char *buffer, size_t size) {
    struct termios orig;
	/// The current number of characters in the buffer.
    size_t len = 0;
    int hist_count = get_history_count();
	/// The history entry (from the 'history' ring buffer) which is currently selected.
    int cursor = hist_count; 
	/// Stores the current buffer content when navigating history, so it can be restored if the user goes back to the "current" line.
    char backup[MAX_PROMPT_LEN] = "";

    if (enable_raw_mode(&orig) == -1) {
        fgets(buffer, size, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';
        return 0;
    }
	// read the buffer one character at a time.
    while (1) {
        char c;
        read(STDIN_FILENO, &c, 1);

        if (c == '\n') {
            buffer[len] = '\0';
            printf("\n");
            break;
        }

		// 127 is the ASCII code for backspace.
        if (c == KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                buffer[len] = '\0';
                refresh_prompt_line(buffer);
            }
            continue;
        }

        if (c == '\t') {
            handle_tab(buffer, &len);
            continue;
        }

        if (c == KEY_ESCAPE) {
            char seq[2];
            read(STDIN_FILENO, &seq[0], 1);
            read(STDIN_FILENO, &seq[1], 1);

			// ESC + A is the ANSI escape sequence for the up arrow key
            if (seq[1] == 'A' && cursor > 0) {
                if (cursor == hist_count)
                    strcpy(backup, buffer);
                cursor--;
                strcpy(buffer, get_history_entry(cursor));
                len = strlen(buffer);
                refresh_prompt_line(buffer);
            }

			// ESC + B is the ANSI escape sequence for the down arrow key.
            if (seq[1] == 'B' && cursor < hist_count) {
                cursor++;
                if (cursor == hist_count)
                    strcpy(buffer, backup);
                else
                    strcpy(buffer, get_history_entry(cursor));
                len = strlen(buffer);
                refresh_prompt_line(buffer);
            }
            continue;
        }

        if (len < size - 1) {
            buffer[len++] = c;
            buffer[len] = '\0';
            putchar(c);
			fflush(stdout);
        }
    }

    disable_raw_mode(&orig);
    return 0;
}

int run_prompt(char *const argv[]) {
    int status = 0;
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        execve(PROMPTER_PATH, argv, environ);
        perror("execve");
        exit(1);
    }

    signals_set_active_child(pid);

    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            if (signals_exit_requested()) {
                kill(pid, SIGTERM);
            }
            continue;
        }

        perror("waitpid");
        signals_clear_active_child();
        return 1;
    }

    signals_clear_active_child();

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

void test() {
    printf("TESTING... \n");
    EnvRecord e = {.name = "test-env",
        .root_path = "/home/yusuf/Desktop/ADAUniversity/OperatingSystems/TeamProject/shocker/.shocker/",
        .pkg_mgr = APT, .pkg_count = 3};

    e.packages = malloc(3 * sizeof(char*));

    e.packages[0] = strdup("git");
    e.packages[1] = strdup("curl");
    e.packages[2] = strdup("vim");

    serialize_env(&e);

    for (int i = 0; i < 3; i++) {
        free(e.packages[i]);
    }
    free(e.packages);

    EnvRecord loaded;
    deserialize_env("/home/yusuf/Desktop/ADAUniversity/OperatingSystems/TeamProject/shocker/.shocker/test-env.shockerfile", &loaded);

    printf("Loaded env:\n");
    printf("Name : %s\n", loaded.name);
    printf("Root Path : %s\n", loaded.root_path);
    printf("Package Manager : %d\n", loaded.pkg_mgr);
    printf("Packages count : %d\n", loaded.pkg_count);
    printf("Packages:\n");
    for (int i = 0; i < loaded.pkg_count; i++) {
        printf("  - %s\n", loaded.packages[i]);
    }

    printf("END TESTING\n");
}

int main() {
    char buffer[MAX_PROMPT_LEN];
    char *args[MAX_ARGS];
    signals_init();

    printf("Shocker activated!\n");

    while (1) {
        if (signals_exit_requested()) {
        break;
    }

    printf("Shocker> ");
    fflush(stdout);

    if (read_input(buffer, sizeof(buffer)) == -1) break;

    if (signals_exit_requested()) {
        break;
    }

    if (!buffer[0]) continue;
        add_history(buffer);

        int i = 0;
        char *token = strtok(buffer, " ");
        while (token && i < MAX_ARGS - 1) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        if (!strcmp(args[0], "exit") || !strcmp(args[0], "quit"))
            break;

        //TODO: FOR TESTING ONLY--WILL BE REMOVED BEFORE FINAL BUILD
        if (strcmp(args[0], "test") == 0) {
            test();
        }

        if (!strcmp(args[0], "help")) {
            printf("Commands: help, exit, install, remove, create, destroy\n");
            continue;
        }

        run_prompt(args);
    }
}

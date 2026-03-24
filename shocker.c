#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define PROMPTER_PATH "./prompter"
#define MAX_PROMPT_LEN 1024
#define MAX_ARGS 64

extern char **environ;

int run_prompt(char *const argv[]) {
    int status;
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return -1;
    } else if (pid == 0) {
        execve(PROMPTER_PATH, argv, environ);
        perror("execve failed");
        exit(1);
    } else {
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid failed");
            return -1;
        }
    }
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static void print_help(void) {
    printf("Built-ins:\n");
    printf("  help         Show this help message\n");
    printf("  exit, quit   Exit shocker\n");
    printf("\n");
    printf("All other commands are forwarded to prompter.\n");
}

int prompt(void) {
    char buffer[MAX_PROMPT_LEN];
    char *args[MAX_ARGS];

    printf("Shocker activated!\n");

    while (1) {
        int i;
        char *token;

        printf("Shocker> ");
        if (fgets(buffer, MAX_PROMPT_LEN, stdin) == NULL) {
            printf("\n");
            break;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        if (buffer[0] == '\0') {
            continue;
        }

        i = 0;
        token = strtok(buffer, " \t");
        while (token != NULL && i < MAX_ARGS - 1) {
            args[i++] = token;
            token = strtok(NULL, " \t");
        }
        args[i] = NULL;

        if (i == 0) {
            continue;
        }

        if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
            break;
        }

        if (strcmp(args[0], "help") == 0) {
            print_help();
            continue;
        }

        {
            int status = run_prompt(args);
            if (status != 0) {
                printf("Error code: %d\n", status);
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        return prompt();
    } else {
        int status = run_prompt(argv + 1);
        return status == -1 ? 1 : status;
    }
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

void getTokens(char **tokens, char *line, const char *delim)
{
    char *token = strtok(line, " ");
    int i = 0;
    while (token != NULL && i < 19)
    {
        tokens[i] = token;
        token = strtok(NULL, delim);
        i++;
    }

    tokens[i] = NULL;
}

void processTask(char **tokens)
{

    char *command = tokens[0];
    if (strcmp(command, "exit") == 0)
    {
        exit(0);
    }

    pid_t pid = fork();
    char *const *args = (char *const *)(tokens);
    if (pid < 0)
    {
        fprintf(stderr, "Couldn't process the command\n");
    }
    else if (pid == 0)
    {

        execvp(tokens[0], args);
        fprintf(stderr, "Error executing ls command\n");
    }
    else
    {
        int status;
        wait(&status);
        if (WIFSIGNALED(status))
        {
            fprintf(stderr, "Command ls ended with signal %d\n", WTERMSIG(status));
        }
    }
}

int main()
{
    while (1)
    {
        char *line = NULL;
        size_t len = 0;
        ssize_t read_chars;

        printf("myshell> ");

        read_chars = getline(&line, &len, stdin);

        if (read_chars != -1)
        {
            line[read_chars - 1] = '\0';
            char *token_line = malloc(100 * sizeof(char));
            strcpy(token_line, line);
            char **tokens = malloc(20 * sizeof(char *));
            getTokens(tokens, token_line, " ");
            processTask(tokens);

            free(token_line);
            free(line);
            free(tokens);
        }
        else
        {
            perror("Error reading input");
        }
    }
    return 0;
}
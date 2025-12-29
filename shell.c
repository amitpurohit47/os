#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>



int getTokens(char **tokens, char *line, const char *delim) {
         char *token = strtok(line, delim);
	 int i = 0;
	 while (token != NULL && i < 100) {
		tokens[i] = token;
		token = strtok(NULL, delim);
		i++;
	 }

	 tokens[i] = NULL; 
	 return i;
}

void processCommands(char **command1, char **command2) {

	
	int fd[2];

	if (pipe(fd) == -1) {
		fprintf(stderr, "Couldn't create pipe\n");
		return;
	}

	pid_t pid1 = fork();

	if (pid1 == 0) {
		close(fd[0]);

		dup2(fd[1], 1);

		close(fd[0]);
		close(fd[1]);

		execvp(command1[0], command1);
		perror("Command execution failed\n");
		exit(1);
	}

	pid_t pid2 = fork();

	if (pid2 == 0) {
		close(fd[1]);

		dup2(fd[0], 0);

		close(fd[0]);
		close(fd[1]);

		execvp(command2[0], command2);
		perror("Command execution failed\n");
		exit(1);
	}

	close(fd[0]);
	close(fd[1]);

	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);
	
}



void processTask(char **tokens, int tokensSize) {
	
	char *lastToken = tokens[tokensSize - 1];
	char *command = tokens[0];
	bool bgProcess = false;
	if (strcmp(command, "exit") == 0) {
		exit(0);
	}

	int pipeIndex = -1;


	for (int i = 0; i < tokensSize; i++)  {
		if (strcmp(tokens[i], "|") == 0) {
			pipeIndex = i;
			break;
		}
	}



	if (pipeIndex != -1) {
		char **command2 = &tokens[pipeIndex + 1];
		tokens[pipeIndex] = NULL;
		processCommands(tokens, command2);
		return;
	}

	

	if (strcmp(lastToken, "&") == 0) {
		bgProcess = true;
		tokens[tokensSize - 1] = NULL;
	}

	
		pid_t pid = fork();
		char * const *args = (char * const *)(tokens);
		if (pid < 0) {
			fprintf(stderr, "Couldn't process the command\n");
		} else if (pid == 0) {
			
			execvp(tokens[0], args);
			fprintf(stderr, "Error executing %s command\n", command);
			exit(1);
		} else {
			int status;
			if (!bgProcess) {
				wait(&status);
				if (WIFSIGNALED(status)) {
					fprintf(stderr, "Command %s ended with signal %d\n", command, WTERMSIG(status));
				}
			}
		}
	
}

/*
void processPiped(char ***commands, int commandsSize, int i) {

	if (i == commandsSize - 1) {
		return;
	} else {
		pid_t pid = fork();

		int fd[2];

		if (pipe(fd) == -1) {
			fprintf("Couldn't create pipe\n");
			return;
		}

		if (pid == 0) {
			close(fd[1]);

			dup2(fd[0], 0);

			close(fd[0]);
			close(fd[1]);

			execvp(commands[i + 1][0], commands[i + 1]);

		} else {
			close(fd[0]);

			dup2(fd[1], 1);

			close(fd[0]);
			close(fd[1]);
			execvp(commands[i][0], commands[i]);
		}
	}
}
*/

int main() {
	 while(1) {
		char *line = NULL;
		size_t len = 0;
		ssize_t read_chars;

		int status;

		while (waitpid(-1, &status, WNOHANG) > 0) {
			// do nothing for now
		}

		printf("myshell> ");

		read_chars = getline(&line, &len, stdin);

		if (read_chars != -1) {
			line[read_chars - 1] = '\0';
			char* token_line = malloc(100 * sizeof(char));
			strcpy(token_line, line);
			char **tokens = malloc(101 * sizeof(char*));
			int tokensSize = getTokens(tokens, token_line, " ");
			processTask(tokens, tokensSize);

			
			
			free(token_line);
			free(line);
			free(tokens);

		
		} else {
			perror("Error reading input");
		}
	}
	return 0;
}


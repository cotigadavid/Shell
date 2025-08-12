#include <limits.h>
#include <sys/wait.h>
#include "internalfuncs.h"

size_t parse_input(char* buffer, char* cmds[], size_t max_cmds) {
    size_t cmdc = 0;
    char* token;
    char* saveptr;

    token = strtok_r(buffer, "|", &saveptr);
    while (token && cmdc < max_cmds - 1) {
        
        while (*token == ' ' || *token == '\t')
            token++;

        cmds[cmdc++] = token;
        token = strtok_r(NULL, "|", &saveptr);
    }

    return cmdc;
}

command* parse_cmd(char* cmd_as_string, size_t max_args) {
    command* new_cmd = (command*)malloc(sizeof(command));
    
    if (new_cmd == NULL) {
        perror("allocation failed");
        return NULL;
    }

    *new_cmd = command_default;

    char* curr_token;
    char* tokens[MAX_ARGS];
    char* saveptr;
    size_t counter = 0;

    curr_token = strtok_r(cmd_as_string, " \t", &saveptr);
    while (curr_token && counter < max_args - 1) {
        tokens[counter++] = curr_token;
        curr_token = strtok_r(NULL, " \t", &saveptr);
    }


    for (size_t i = 0; i < counter; ++i) {
        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after <");
                return NULL;
            }
            
            new_cmd->redirectInput = tokens[i + 1];
            new_cmd->appendOutput = 0;
            i++;
        }
        else if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after >");
                return NULL;
            }
            new_cmd->redirectOutput = tokens[i + 1];
            new_cmd->appendOutput = 0;
            i++;
        }
        else if (strcmp(tokens[i], ">>") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after >>");
                return NULL;
            }
            new_cmd->redirectOutput = tokens[i + 1];
            new_cmd->appendOutput = 1;
            i++;
        }
        else {
            new_cmd->argv[new_cmd->argc++] = tokens[i];
        }
    }


    new_cmd->argv[new_cmd->argc] = NULL;
    return new_cmd;
}

void duplicate_fd(command* cmd) {
    if (cmd->redirectInput != NULL) {
        int fd = open(cmd->redirectInput, O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "Could not open file");
            free(cmd);
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDIN_FILENO) < 0) { 
            perror("dup2 stdin failed");
            free(cmd);
            exit(EXIT_FAILURE);
        }
        close(fd);
    }

    if (cmd->redirectOutput != NULL) {
        int flags = O_WRONLY | O_CREAT;

        if (cmd->appendOutput) 
            flags |= O_APPEND;
        else
            flags |= O_TRUNC;

        int fd = open(cmd->redirectOutput, flags, 0644);

        if (fd < 0) {
            fprintf(stderr, "Could not open file");
            free(cmd);
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 stdout failed");
            free(cmd);
            exit(EXIT_FAILURE);
        }
        
        close(fd);
    }
}

void execute_pipeline(size_t cmdc, char* cmds[]) {
    int pipefds[2 * (cmdc - 1)];
    pid_t pids[cmdc];

    for (size_t i = 0; i < cmdc - 1; ++i) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe failed");
            return;
        }
    }

    for (size_t i = 0; i < cmdc; ++i) {
        command* cmd = parse_cmd(cmds[i], MAX_ARGS);

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // child

            if (i > 0) { // all but first cmd
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) < 0) {
                    perror("dup2 stdin");
                    free(cmd);
                    exit(EXIT_FAILURE);
                }
            }

            if (i < cmdc - 1) { // all but last cmd
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) < 0) {
                    perror("dup2 stdout");
                    free(cmd);
                    exit(EXIT_FAILURE);
                }
            }

            for (size_t j = 0; j < 2 * (cmdc - 1); ++j) {
                close(pipefds[j]);
            }
            
            internal_func func = get_internal_func(cmd->argv[0]);

            duplicate_fd(cmd);

            if (func != NULL) {
                func(cmd);
                free(cmd);
                exit(EXIT_SUCCESS);
            }
            else {
                execvp(cmd->argv[0], cmd->argv);
                perror("execvp failed");
                free(cmd);
                exit(EXIT_FAILURE);
            }
        }
        else {  // parent
            pids[i] = pid;
        }
    }

    for (size_t i = 0; i < 2 * (cmdc - 1); ++i) {
        close(pipefds[i]);
    }
    for (size_t i = 0; i < cmdc; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

int main() {
    char* buffer = NULL;
    size_t bufferSize = 0;
    ssize_t lineLen;

    while (1) {
        printf("shell:~");
        internal_pwd(&command_default);
        printf("$ ");
        fflush(stdout);

        lineLen = getline(&buffer, &bufferSize, stdin);
        if (lineLen == -1) {
            if (feof(stdin)) {
                printf("\nExit shell.\n");
            } else {
                perror("getline");
            }
            break;
        }

        if (lineLen > 0 && buffer[lineLen - 1] == '\n') {
            buffer[lineLen - 1] = '\0';
            lineLen--;
        }

        if (lineLen == 0) continue;

        char* cmds[MAX_CMDS];
        size_t cmdc = parse_input(buffer, cmds, MAX_CMDS);

        if (cmdc == 1) {
            command* cmd = parse_cmd(cmds[0], MAX_ARGS);
            
            if (cmd->argc > 0) {
                // Check if parent built-in
                if (is_parent_builtin(cmd->argv[0])) {
                    internal_func func = get_internal_func(cmd->argv[0]);
                    if (func != NULL) {
                        func(cmd);
                        free(cmd);
                        continue;
                    }
                }
                
                internal_func func = get_internal_func(cmd->argv[0]);
                
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork failed");
                    free(cmd);
                    continue;
                }
                
                if (pid == 0) {
                    
                    duplicate_fd(cmd);

                    if (func != NULL) {
                        func(cmd);
                        free(cmd);
                        exit(EXIT_SUCCESS);
                    }

                    execvp(cmd->argv[0], cmd->argv);
                    perror("execvp failed");
                    free(cmd);
                    exit(EXIT_FAILURE);
                } else {
                    // Parent process
                    int status;
                    waitpid(pid, &status, 0);
                    continue;
                }
            }
        }

        execute_pipeline(cmdc, cmds);
    }

    free(buffer);
    return 0;
}
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include "internalfuncs.h"

static void free_pipeline_mem(pipeline* p) {
    if (!p) return;
    for (size_t i = 0; i < p->cmdc; i++) {
        if (p->cmds[i]) free(p->cmds[i]); // argv/redirects point inside p->buffer
    }
    free(p->buffer);
    free(p);
}

static command* parse_cmd(char* cmd_as_string, size_t max_args) {
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

static pipeline* parse_input(char* buffer, size_t max_cmds) {
    if (!buffer) return NULL;

    pipeline* new_pipeline = (pipeline*)malloc(sizeof(pipeline));
    if (new_pipeline == NULL) { 
        perror("malloc"); 
        return NULL; 
    }
    memset(new_pipeline, 0, sizeof(*new_pipeline));

    new_pipeline->buffer = strdup(buffer);
    if (new_pipeline->buffer == NULL) { 
        perror("strdup"); 
        free(new_pipeline); 
        return NULL; }

    char* saveptr = NULL;
    char* token = strtok_r(new_pipeline->buffer, "|", &saveptr);

    while (token && new_pipeline->cmdc < max_cmds) {
        while (*token == ' ' || *token == '\t')
            token++;
        
        command* cmd = parse_cmd(token, MAX_ARGS);
        if (!cmd) { 
            free_pipeline_mem(new_pipeline); 
            return NULL; 
        }
        new_pipeline->cmds[new_pipeline->cmdc++] = cmd;
        token = strtok_r(NULL, "|", &saveptr);
    }

    if (new_pipeline->cmdc == 0) 
        return new_pipeline; 

    command* last = new_pipeline->cmds[new_pipeline->cmdc - 1];
    if (last->argc > 0 && strcmp(last->argv[last->argc - 1], "&") == 0) {
        last->argv[--last->argc] = NULL;
        new_pipeline->background = 1;
    }
    return new_pipeline;
}

static void duplicate_fd(command* cmd) {
    if (cmd->redirectInput != NULL) {
        int fd = open(cmd->redirectInput, O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "Could not open file");
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDIN_FILENO) < 0) { 
            perror("dup2 stdin failed");
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
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 stdout failed");
            exit(EXIT_FAILURE);
        }
        
        close(fd);
    }
}

static volatile sig_atomic_t child_status_changed = 0;

void sigchld_handler(int sig) {
    child_status_changed = 1;
}

void install_sigchld_handler(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

void check_child_status(void) {
    if (!child_status_changed) {
        return;  // Nothing to do
    }
    
    child_status_changed = 0;  // Reset flag
    
    int status;
    pid_t pid;
    
    // Process all available child status changes
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        pid_t pgid = get_pgid_of_process(pid);
        if (pgid == -1) {
            continue;  // Skip if we can't get process group
        }
        
        job_t* job = find_job_by_pgid(pgid);
        if (job == NULL) {
            continue;  // Not a background job we're tracking
        }

        // Update job status
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            update_job_status(job, pid, JOB_DONE);
        } else if (WIFSTOPPED(status)) {
            update_job_status(job, pid, JOB_STOPPED);
        } else if (WIFCONTINUED(status)) {
            update_job_status(job, pid, JOB_RUNNING);
        }
    }
}

static void execute_pipeline(pipeline* curr_pipeline) {

    int pipefds[2 * (curr_pipeline->cmdc - 1)];
    pid_t pids[curr_pipeline->cmdc];

    for (size_t i = 0; i < curr_pipeline->cmdc - 1; ++i) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    for (size_t i = 0; i < curr_pipeline->cmdc; ++i) {
        command* cmd = curr_pipeline->cmds[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) { // CHILD
            // Put child in process group
            setpgid(0, (i == 0) ? 0 : pids[0]);

            // Set up pipes
            if (i > 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i < curr_pipeline->cmdc - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            for (size_t j = 0; j < 2 * (curr_pipeline->cmdc - 1); ++j)
                close(pipefds[j]);

            internal_func func = get_internal_func(cmd->argv[0]);
            duplicate_fd(cmd);

            if (func) {
                func(cmd);
                _exit(0);
            } else {
                execvp(cmd->argv[0], cmd->argv);
                perror("execvp");
                _exit(1);
            }
        } 
        else { // PARENT
            pids[i] = pid;
            setpgid(pid, (i == 0) ? pid : pids[0]);
        }
    }

    for (size_t i = 0; i < 2 * (curr_pipeline->cmdc - 1); ++i)
        close(pipefds[i]);

    if (curr_pipeline->background == 0) {
        // Foreground job: wait for all
        for (size_t i = 0; i < curr_pipeline->cmdc; ++i) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    } else {
        // Background job
        int j_id = add_job(pids[0], curr_pipeline->buffer, curr_pipeline)->job_id;
        printf("[%d] PGID: %ld\n", j_id, (long)pids[0]);
        for (size_t i = 0; i < curr_pipeline->cmdc; ++i)
            add_process_to_job(pids[0], pids[i]);
    }
}


int main() {
    // struct sigaction sa;
    // sa.sa_handler = sigchld_handler;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = SA_RESTART;
    // sigaction(SIGCHLD, &sa, NULL);

    install_sigchld_handler();

    char* buffer = NULL;
    size_t bufferSize = 0;
    ssize_t lineLen;

    while (1) {

        printf("shell:~");
        internal_pwd(&command_default);
        printf("$ ");
        fflush(stdout);

        check_child_status();

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

        pipeline* curr_pipeline = parse_input(buffer, MAX_CMDS);
        if (curr_pipeline == NULL) continue;

        if (curr_pipeline->cmdc == 1) {
            command* cmd = curr_pipeline->cmds[0];
            
            if (cmd->argc > 0) {
                // Check if parent built-in
                if (is_parent_builtin(cmd->argv[0])) {
                    internal_func func = get_internal_func(cmd->argv[0]);
                    if (func != NULL) {
                        func(cmd);
                        free_pipeline_mem(curr_pipeline);
                        continue;
                    }
                }
                
                internal_func func = get_internal_func(cmd->argv[0]);
                
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork failed");
                    free_pipeline_mem(curr_pipeline);
                    continue;
                }
                
                if (pid == 0) {  //child
                    duplicate_fd(cmd);
                    setpgid(0, 0);

                    if (func != NULL) {
                        func(cmd);
                        exit(EXIT_SUCCESS);
                    }

                    execvp(cmd->argv[0], cmd->argv);
                    perror("execvp failed");
                    exit(EXIT_FAILURE);
                } else {
                    // Parent process
                    setpgid(pid, pid);

                    if (curr_pipeline->background == 0) {
                        int status;
                        waitpid(pid, &status, 0);
                    }
                    else {
                        int j_id = add_job(pid, curr_pipeline->buffer, curr_pipeline)->job_id;
                        add_process_to_job(pid, pid);
                        printf("[%d] PGID: %ld\n", j_id, (long)pid);
                    }
                    free_pipeline_mem(curr_pipeline);
                    continue;
                }
            }
        }

        check_child_status();

        execute_pipeline(curr_pipeline);
        free_pipeline_mem(curr_pipeline);
    }

    free(buffer);
    return 0;
}

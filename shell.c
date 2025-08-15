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

static void give_terminal_to(pid_t pgid) {
    if (!shell_interactive) return;
    if (pgid <= 0) return;
    tcsetpgrp(shell_tty, pgid);
}

static void reclaim_terminal(void) {
    if (!shell_interactive) return;
    tcsetpgrp(shell_tty, shell_pgid);
}

static volatile sig_atomic_t child_status_changed = 0;

void sigchld_handler(int sig) {
    (void)sig;
    child_status_changed = 1;
}

static void sigint_handler(int sig) {
    (void)sig;
    pid_t pgid = fg_pgid;
    if (pgid > 0) killpg(pgid, SIGINT);
}

static void sigtstp_handler(int sig) {
    (void)sig;
    pid_t pgid = fg_pgid;
    if (pgid > 0) killpg(pgid, SIGTSTP);
}

static void ignore_signal(int signo) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signo, &sa, NULL);
}

static void set_handler(int signo, void (*handler)(int)) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signo, &sa, NULL);
}

static void install_all_shell_handlers(void) {
    set_handler(SIGCHLD, sigchld_handler);

    set_handler(SIGINT,  sigint_handler);
    set_handler(SIGTSTP, sigtstp_handler);

    ignore_signal(SIGTTOU);
    ignore_signal(SIGTTIN);
}

void check_child_status(void) {
    if (!child_status_changed) {
        return;
    }
    
    child_status_changed = 0;
    
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        pid_t pgid = get_pgid_of_process(pid);
        if (pgid == -1) {
            continue; 
        }
        
        job_t* job = find_job_by_pgid(pgid);
        if (job == NULL) {
            continue;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            update_job_status(job, pid, JOB_DONE);
        } else if (WIFSTOPPED(status)) {
            update_job_status(job, pid, JOB_STOPPED);
        } else if (WIFCONTINUED(status)) {
            update_job_status(job, pid, JOB_RUNNING);
        }
    }
}

static void setup_child_signals_and_pgrp(pid_t pg_leader_pgid, int is_fg) {

    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    if (pg_leader_pgid == 0) {
        setpgid(0, 0);
    } else {
        setpgid(0, pg_leader_pgid);
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

    int is_fg = (curr_pipeline->background == 0);
    pid_t pg_leader = 0;

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    for (size_t i = 0; i < curr_pipeline->cmdc; ++i) {
        command* cmd = curr_pipeline->cmds[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) { // CHILD
            sigprocmask(SIG_SETMASK, &oldmask, NULL);
            
            setup_child_signals_and_pgrp((i == 0) ? 0 : pg_leader, is_fg);

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
            if (i == 0) {
                setpgid(pid, pid);
                pg_leader = pid;
            } else {
                setpgid(pid, pg_leader);
            }
        }
    }

    for (size_t i = 0; i < 2 * (curr_pipeline->cmdc - 1); ++i)
        close(pipefds[i]);

    job_t* job = NULL;
    if (!is_fg) {
        job = add_job(pg_leader, curr_pipeline->buffer, curr_pipeline);
        for (int i = 0; i < curr_pipeline->cmdc; ++i) {
            add_process_to_job(pg_leader, pids[i]);
        }
        printf("[%d] PGID: %ld\n", job->job_id, (long)pg_leader);
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    if (is_fg) {
        fg_pgid = pg_leader;
        give_terminal_to(pg_leader);

        int alive = curr_pipeline->cmdc;
        while (alive > 0) {
            int status = 0;
            pid_t w = waitpid(-1, &status, WUNTRACED);
            if (w < 0) {
                if (errno == EINTR) continue;
                if (errno == ECHILD) break;
                perror("waitpid");
                break;
            }
            if (w == 0) continue;

            pid_t wpgid = get_pgid_of_process(w);
            if (wpgid != pg_leader) continue; 

            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                alive--;
            } else if (WIFSTOPPED(status)) {
                if (!job) {
                    job = add_job(pg_leader, curr_pipeline->buffer, curr_pipeline);
                    for (int i = 0; i < curr_pipeline->cmdc; ++i) {
                        add_process_to_job(pg_leader, pids[i]);
                    }
                    printf("\n[%d]+  Stopped\t%s\n", job->job_id, curr_pipeline->buffer);
                }
                update_job_status(job, w, JOB_STOPPED);
                break;
            }
        }

        reclaim_terminal();
        fg_pgid = 0;
    }
}

static void execute_single_command(pipeline* curr_pipeline) {
    command* cmd = curr_pipeline->cmds[0];
    
    if (cmd->argc == 0) return;
    
    // Check if parent built-in
    if (is_parent_builtin(cmd->argv[0])) {
        internal_func func = get_internal_func(cmd->argv[0]);
        if (func != NULL) {
            func(cmd);
            return;
        }
    }
    
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        return;
    }

    if (pid == 0) {
        // CHILD
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        
        int is_fg = (curr_pipeline->background == 0);
        setup_child_signals_and_pgrp(0, is_fg);
        duplicate_fd(cmd);

        internal_func func = get_internal_func(cmd->argv[0]);
        if (func != NULL) {
            func(cmd);
            _exit(EXIT_SUCCESS);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp failed");
        _exit(EXIT_FAILURE);
    } else {
        // PARENT
        setpgid(pid, pid);

        job_t* job = NULL;
        if (curr_pipeline->background != 0) {
            job = add_job(pid, curr_pipeline->buffer, curr_pipeline);
            add_process_to_job(pid, pid);
            printf("[%d] PGID: %ld\n", job->job_id, (long)pid);
        }

        sigprocmask(SIG_SETMASK, &oldmask, NULL);

        if (curr_pipeline->background == 0) {
            fg_pgid = pid;
            give_terminal_to(pid);

            int status;
            while (1) {
                pid_t w = waitpid(pid, &status, WUNTRACED);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    perror("waitpid");
                    break;
                }
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    // done
                    break;
                }
                if (WIFSTOPPED(status)) {
                    if (!job) {
                        job = add_job(pid, curr_pipeline->buffer, curr_pipeline);
                        add_process_to_job(pid, pid);
                        printf("\n[%d]+  Stopped\t%s\n", job->job_id, curr_pipeline->buffer);
                    }
                    update_job_status(job, pid, JOB_STOPPED);
                    break;
                }
            }

            reclaim_terminal();
            fg_pgid = 0;
        }
    }
}

int main() {
    shell_tty = STDIN_FILENO;
    shell_interactive = isatty(shell_tty);

    if (shell_interactive) {
        shell_pgid = getpgrp();
        if (getpid() != shell_pgid) {
            if (setpgid(0, 0) < 0) {
                perror("setpgid");
                exit(1);
            }
            shell_pgid = getpgrp();
        }

        tcsetpgrp(shell_tty, shell_pgid);
    }

    install_all_shell_handlers();

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
            execute_single_command(curr_pipeline);
        } else {
            execute_pipeline(curr_pipeline);
        }

        check_child_status();
        free_pipeline_mem(curr_pipeline);
    }

    free(buffer);
    return 0;
}
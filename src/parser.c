#include "../headers/parser.h"
#include "../headers/variables.h"

int shell_interactive = 0;
pid_t shell_pgid = 0;
int shell_tty = -1;
volatile sig_atomic_t fg_pgid = 0;
volatile sig_atomic_t child_status_changed = 0;

void give_terminal_to(pid_t pgid) {
    if (!shell_interactive) return;
    if (pgid <= 0) return;
    tcsetpgrp(shell_tty, pgid);
}

void reclaim_terminal(void) {
    if (!shell_interactive) return;
    tcsetpgrp(shell_tty, shell_pgid);
}

void sigchld_handler(int sig) {
    (void)sig;
    child_status_changed = 1;
}

void sigint_handler(int sig) {
    (void)sig;
    pid_t pgid = fg_pgid;
    if (pgid > 0) killpg(pgid, SIGINT);
}

void sigtstp_handler(int sig) {
    (void)sig;
    pid_t pgid = fg_pgid;
    if (pgid > 0) killpg(pgid, SIGTSTP);
}

void ignore_signal(int signo) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signo, &sa, NULL);
}

void set_handler(int signo, void (*handler)(int)) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signo, &sa, NULL);
}

void install_all_shell_handlers(void) {
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

void setup_child_signals_and_pgrp(pid_t pg_leader_pgid, int is_fg) {

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
        if (tokens[i][0] == '$') {
            char* temp = tokens[i];

            tokens[i] = get_var(temp + 1);

            if (tokens[i] == NULL) {
                fprintf(stderr, "no variable with this name\n");
                return NULL;
            }
        }

        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after <\n");
                return NULL;
            }
            
            new_cmd->redirectInput = tokens[i + 1];
            new_cmd->appendOutput = 0;
            i++;
        }
        else if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after >\n");
                return NULL;
            }
            new_cmd->redirectOutput = tokens[i + 1];
            new_cmd->appendOutput = 0;
            i++;
        }
        else if (strcmp(tokens[i], ">>") == 0) {
            if (i + 1 == counter) {
                fprintf(stderr, "no filename after >>\n");
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

pipeline* parse_input(char* buffer, size_t max_cmds) {
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
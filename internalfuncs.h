#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "proc.h"

#define MAX_ARGS 100
#define MAX_CMDS 100
#define MAX_PROCESSES 100
#define MAX_GLOBAL_PROCESSES 10000
#define BUFFER_SIZE 4096

struct command_inter {
    size_t argc;
    char* argv[MAX_ARGS];
    char* redirectInput;
    char* redirectOutput;
    int appendOutput; // either 0 or 1
} command_default = {0, NULL, NULL, NULL, 0, 0};

typedef struct command_inter command;

struct pipeline_inter {
    size_t cmdc;
    command* cmds[MAX_CMDS];
    int background; // either 0 or 1
    char* buffer;

} pipeline_default = {0, NULL, 0};

typedef struct pipeline_inter pipeline;

typedef void(*internal_func)(const command*);

typedef struct {
    char* name;
    internal_func fptr;
    int run_in_parent;
} internal_pair;

void internal_echo(const command*);
void internal_pwd(const command*);
void internal_cd(const command*);
void internal_ls(const command*);
void internal_cat(const command*);
void internal_jobs(const command*);
void internal_fg(const command*);
void internal_bg(const command*);

internal_pair internals[] = {
    {"echo", internal_echo, 0},  // can run in child
    {"pwd", internal_pwd, 0},    // can run in child  
    {"cd", internal_cd, 1},      // MUST run in parent
    {"ls", internal_ls, 0},      // can run in child
    {"cat", internal_cat, 0},    // can run in child
    {"jobs", internal_jobs, 0},    // can run in child
    {"fg", internal_fg, 0},    // can run in child
    {"bg", internal_bg, 0},    // can run in child
    {NULL, NULL, 0}
};

internal_func get_internal_func(char* cmd) {
    for (int i = 0; internals[i].name != NULL; ++i) {
        if (strcmp(cmd, internals[i].name) == 0)
            return internals[i].fptr;
    }
    return NULL;
}

int is_parent_builtin(char* cmd) {
    for (int i = 0; internals[i].name != NULL; ++i) {
        if (strcmp(cmd, internals[i].name) == 0)
            return internals[i].run_in_parent;
    }
    return 0;
}

void internal_echo(const command* cmd) {
    for (size_t i = 1; i < cmd->argc; ++i)
        printf("%s", cmd->argv[i]);
}

void internal_pwd(const command* cmd) {
    char cwd[PATH_MAX];
    
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s", cwd);
    }
    else {
        perror("pwd failed");
    }
}

void internal_cd(const command* cmd) {
    char* target_dir;
    
    if (cmd->argc == 1) {
        target_dir = getenv("HOME");
        if (target_dir == NULL) {
            target_dir = "/";
        }
    }
    else {
        target_dir = cmd->argv[1];
    }
    
    if (chdir(target_dir) != 0) {
        perror("cd");
    }
}

void internal_ls(const command* cmd) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        printf("pwd failed");
        return;
    }

    DIR* dir = opendir(cwd);

    if (!dir) {
        perror("opendir failed");
        return;
    }

    struct dirent* dr;

    while ((dr = readdir(dir))) {
        if (dr->d_name[0] != '.')
            printf("%s\n", dr->d_name);
    }
    closedir(dir);
}

void internal_cat(const command* cmd) {
    if (cmd->argc < 2) {
        // cat with no arguments - read from stdin
        char buffer[BUFFER_SIZE];
        ssize_t bytesRead;
        while ((bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
            if (write(STDOUT_FILENO, buffer, bytesRead) == -1) {
                perror("write failed");
                break;
            }
        }
        if (bytesRead == -1) {
            perror("read failed");
        }
        return;
    }

    // cat with file arguments
    for (size_t i = 1; i < cmd->argc; ++i) {
        int fd = open(cmd->argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "cat: %s: %s\n", cmd->argv[i], strerror(errno));
            continue;
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytesRead;
        while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
            if (write(STDOUT_FILENO, buffer, bytesRead) == -1) {
                perror("write failed");
                break;
            }
        }
        
        if (bytesRead == -1) {
            perror("read failed");
        }
        
        close(fd);
    }
}

void internal_jobs(const command* cmd) {
    print_jobs();
}

void internal_fg(const command* cmd) {
    job_t* job = NULL;

    if (cmd->argc > 1) {
        job = find_job_by_id((long)*cmd->argv[1] - 48);
    }
    else {
        fprintf(stderr, "fg: no job id\n");
    }

    if (!job) {
        fprintf(stderr, "fg: no JOB found\n");
        return;
    }

    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("kill SIGCONT");
        return;
    }

    if (tcsetpgrp(STDIN_FILENO, job->pgid) < 0) {
        perror("tcsetpgrp");
        return;
    }

    update_all_processes_in_job(job, JOB_RUNNING);
    
    printf("%s\n", job->command_line);

    int status;
    pid_t pid;
    
    // Wait for all processes in this job
    while (job->status != JOB_DONE && !job_is_stopped(job)) {
        pid = waitpid(-job->pgid, &status, WUNTRACED);
        if (pid > 0) {
            // Handle status change
            if (WIFSTOPPED(status)) {
                update_job_status(job, pid, JOB_STOPPED);
                break;
            } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                update_job_status(job, pid, JOB_DONE);
            }
        }
    }
    
    if (tcsetpgrp(STDIN_FILENO, getpid()) < 0) {
        perror("tcsetpgrp restore");
    }
    
    //REMOVE JOB WHEN COMPLETED
}

void internal_bg(const command* cmd) {
    job_t* job = NULL;

    if (cmd->argc > 1) {
        job = find_job_by_id(cmd->argv[1]);
    }
    else {
        fprintf(stderr, "bg: no job id\n");
    }

    if (!job) {
        fprintf(stderr, "bg: no job found\n");
        return;
    }
    
    if (job->status == JOB_RUNNING) {
        printf("job is already running\n");
        return;
    }
    
    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("kill SIGCONT");
        return;
    }
    
    update_all_processes_in_job(job, JOB_RUNNING);
    
    printf("[%d] %s &\n", job->job_id, job->command_line);
}
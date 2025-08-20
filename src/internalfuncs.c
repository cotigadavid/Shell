#include "../headers/internalfuncs.h"

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
    char buffer[BUFFER_SIZE];

    for (size_t i = 1; i < cmd->argc; ++i) {
        int size = snprintf(buffer, BUFFER_SIZE, "%s ", cmd->argv[i]);
        write(STDOUT_FILENO, buffer, strlen(buffer));
    }
    int size = snprintf(buffer, BUFFER_SIZE, "\n");
    write(STDOUT_FILENO, buffer, strlen(buffer));
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
        char* job_str = cmd->argv[1];
        int job_id;
        
        if (job_str[0] == '%') {
            job_id = atoi(job_str + 1);
        } else {
            job_id = atoi(job_str);
        }
        
        job = find_job_by_id(job_id);
    }
    else {
        fprintf(stderr, "fg: no job id\n");
    }

    if (!job) {
        fprintf(stderr, "fg: no JOB found\n");
        return;
    }

    if (job->status == JOB_RUNNING && fg_pgid == job->pgid) {
        fprintf(stderr, "fg: job already in foreground\n");
        return;
    }

    fg_pgid = job->pgid;

    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("kill SIGCONT");
        fg_pgid = 0;
        return;
    }

    if (tcsetpgrp(STDIN_FILENO, job->pgid) < 0) {
        perror("tcsetpgrp");
        fg_pgid = 0;
        return;
    }

    update_all_processes_in_job(job, JOB_RUNNING);
    
    printf("%s\n", job->command_line);

    int status;
    pid_t pid;
    int processes_remaining = count_processes_in_job(job); 
    
    while (processes_remaining > 0) {
        pid = waitpid(-1, &status, WUNTRACED);
        
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            perror("waitpid");
            break;
        }
        
        if (pid == 0) continue;
        
        pid_t process_pgid = get_pgid_of_process(pid);
        if (process_pgid != job->pgid) continue; 
        
        if (WIFSTOPPED(status)) {
            update_job_status(job, pid, JOB_STOPPED);
            printf("\n[%d]+  Stopped\t%s\n", job->job_id, job->command_line);
            break; 
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            update_job_status(job, pid, JOB_DONE);
            processes_remaining--;
        }
    }
    
    if (shell_interactive) {
        if (tcsetpgrp(shell_tty, shell_pgid) < 0) {
            perror("tcsetpgrp restore");
        }
    }
    fg_pgid = 0;
    
    if (job->status == JOB_DONE || all_processes_done(job)) {
        remove_job(job);
    }
}

void internal_bg(const command* cmd) {
    job_t* job = NULL;

    if (cmd->argc > 1) {
        char* job_str = cmd->argv[1];
        int job_id;
        
        if (job_str[0] == '%') {
            job_id = atoi(job_str + 1);
        } else {
            job_id = atoi(job_str);
        }
        
        job = find_job_by_id(job_id);
    } else {
        if (!job) {
            fprintf(stderr, "bg: no current job\n");
            return;
        }
    }

    if (!job) {
        fprintf(stderr, "bg: no such job\n");
        return;
    }
    
    if (job->status != JOB_STOPPED && !job_is_stopped(job)) {
        printf("bg: job [%d] already running\n", job->job_id);
        return;
    }
    
    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("kill SIGCONT");
        return;
    }
    
    update_all_processes_in_job(job, JOB_RUNNING);
    
    printf("[%d] %s &\n", job->job_id, job->command_line);
}

void internal_env(const command* cmd) {
    if (cmd->argc == 1)
        print_all_var();
    else {
        fprintf(stderr, "env: invalid format, too many arguments\n");
    }
}

void internal_set(const command* cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "set: invalid format, not enough arguments\n");
        return;
    }
    
    for (int i = 1; i < cmd->argc; i++) {
        char *eq = strchr(cmd->argv[i], '=');
        if (!eq) {
            fprintf(stderr, "set: invalid format, use NAME=value\n");
            continue;
        }

        char *arg_copy = strdup(cmd->argv[i]);
        char *eq_copy = strchr(arg_copy, '=');
        
        *eq_copy = '\0';
        const char *name = arg_copy;
        const char *value = eq_copy + 1;
        
        set_var(name, value); 
        
        free(arg_copy);
    }
}

void internal_export(const const command* cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "Usage: export VAR\n");
        return;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const char *name = cmd->argv[i];
        const char *value = get_var(name); 

        if (!value) {
            fprintf(stderr, "export: variable '%s' not found\n", name);
            continue;
        }

        if (setenv(name, value, 1) != 0) {
            perror("setenv");
            return;
        }
    }
    return;
}

void internal_unset(const const command* cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "Usage: unset VAR\n");
        return;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const char *name = cmd->argv[i];
        unset_var(name);
        unsetenv(name);
    }
    return;
}

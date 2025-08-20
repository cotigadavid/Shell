#include "../headers/execute.h"

#include "../headers/internalfuncs.h"


void duplicate_fd(command* cmd) {
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

void execute_pipeline(pipeline* curr_pipeline) {
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

void execute_single_command(pipeline* curr_pipeline) {
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

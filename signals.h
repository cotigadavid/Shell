#pragma once

#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

static int shell_interactive = 0;
static pid_t shell_pgid = 0;
static int shell_tty = -1;
static volatile sig_atomic_t fg_pgid = 0;
static volatile sig_atomic_t child_status_changed = 0;

static void give_terminal_to(pid_t pgid) {
    if (!shell_interactive) return;
    if (pgid <= 0) return;
    tcsetpgrp(shell_tty, pgid);
}

static void reclaim_terminal(void) {
    if (!shell_interactive) return;
    tcsetpgrp(shell_tty, shell_pgid);
}

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

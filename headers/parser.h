#pragma once

#include "headers.h"

#include "proc.h"
#include "pipelines.h"

extern int shell_interactive;
extern pid_t shell_pgid;
extern int shell_tty;
extern volatile sig_atomic_t fg_pgid;
extern volatile sig_atomic_t child_status_changed;

void give_terminal_to(pid_t pgid);

void reclaim_terminal(void);

void sigchld_handler(int sig);

void sigint_handler(int sig);

void sigtstp_handler(int sig);

void ignore_signal(int signo);

void set_handler(int signo, void (*handler)(int));

void install_all_shell_handlers(void);

void check_child_status(void);

void setup_child_signals_and_pgrp(pid_t pg_leader_pgid, int is_fg);

command* parse_cmd(char* cmd_as_string, size_t max_args);

pipeline* parse_input(char* buffer, size_t max_cmds);
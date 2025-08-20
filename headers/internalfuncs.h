#pragma once

#include "headers.h"

#include "proc.h"
#include "parser.h"
#include "variables.h"
#include "pipelines.h"

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
void internal_env(const command*);
void internal_set(const command*);
void internal_export(const command*);
void internal_unset(const command*);

static internal_pair internals[] = {
    {"echo", internal_echo, 0},
    {"pwd", internal_pwd, 1},    // can run in child  
    {"cd", internal_cd, 1},      // MUST run in parent
    {"ls", internal_ls, 0},
    {"cat", internal_cat, 0},
    {"jobs", internal_jobs, 0},
    {"fg", internal_fg, 1},
    {"bg", internal_bg, 1},
    {"env", internal_env, 1},
    {"set", internal_set, 1},
    {"unset", internal_unset, 1},
    {"export", internal_export, 1},
    {NULL, NULL, 0}
};

internal_func get_internal_func(char* cmd);

int is_parent_builtin(char* cmd);

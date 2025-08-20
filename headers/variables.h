#pragma once

#include "headers.h"

extern char **environ;

typedef struct var {
    char* name;
    char* value;
    struct var* next;
} var_t;

extern var_t* var_list;

void set_var(const char *name, const char *value);

void load_environment();

char* get_var(const char *name);

void print_all_var();

void unset_var(char* name);
#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern char **environ;

typedef struct var {
    char* name;
    char* value;
    struct var* next;
} var_t;

var_t* var_list = NULL;

void set_var(const char *name, const char *value) {
    var_t *curr = var_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            free(curr->value);
            curr->value = strdup(value);
            return;
        }
        curr = curr->next;
    }
    // Not found → add new var
    var_t *new_var = malloc(sizeof(var_t));
    new_var->name = strdup(name);
    new_var->value = strdup(value);
    new_var->next = var_list;
    var_list = new_var;
}

void load_environment() {
    for (char **env = environ; *env != NULL; env++) {
        char *entry = *env;
        char *eq = strchr(entry, '=');
        if (!eq) continue;

        size_t name_len = eq - entry;
        char name[name_len + 1];
        strncpy(name, entry, name_len);
        name[name_len] = '\0';

        const char *value = eq + 1;

        set_var(name, value);
    }
}

char* get_var(const char *name) {
    var_t *curr = var_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return getenv(name); // fallback to environment
}

void print_all_var() {
    var_t* curr = var_list;

    while (curr != NULL) {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
}

void unset_var(char* name) {
    var_t* curr = var_list, *prev = NULL;

    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (prev)
                prev->next = curr->next;
            else
                var_list = curr->next;
            
            //free the mem
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}
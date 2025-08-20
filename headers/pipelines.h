#pragma once

#include "headers.h"

struct command_inter {
    size_t argc;
    char* argv[MAX_ARGS];
    char* redirectInput;
    char* redirectOutput;
    int appendOutput; // either 0 or 1
}; 
typedef struct command_inter command;

extern command command_default;

struct pipeline_inter {
    size_t cmdc;
    command* cmds[MAX_CMDS];
    int background; // either 0 or 1
    char* buffer;
    
};
typedef struct pipeline_inter pipeline;

extern pipeline pipeline_default;

static void free_pipeline_mem(pipeline* p) {
    if (!p) return;
    for (size_t i = 0; i < p->cmdc; i++) {
        if (p->cmds[i]) free(p->cmds[i]); // argv/redirects point inside p->buffer
    }
    free(p->buffer);
    free(p);
}

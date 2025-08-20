#pragma once

#include "headers.h"
#include "internalfuncs.h"

#define ARROW_UP    1000
#define ARROW_DOWN  1001
#define ARROW_RIGHT 1002
#define ARROW_LEFT  1003

#define HISTORY_MAX 100
#define INPUT_BUF 1024

static struct termios orig_termios;

extern char *history[HISTORY_MAX];
extern int history_len;
extern int history_index;

void disable_raw_mode();

void enable_raw_mode();

int read_key();

void add_history(const char *cmd);

void redraw_prompt(const char *buf);

void read_line(char *buf);
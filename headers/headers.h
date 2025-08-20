#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <stdbool.h>

#define MAX_ARGS 100
#define MAX_CMDS 100
#define MAX_PROCESSES 100
#define MAX_GLOBAL_PROCESSES 10000
#define BUFFER_SIZE 4096


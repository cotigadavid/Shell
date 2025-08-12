#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_ARGS 100
#define MAX_CMDS 100
#define BUFFER_SIZE 4096

struct command_inter{
    size_t argc;
    char* argv[MAX_ARGS];
    char* redirectInput;
    char* redirectOutput;
    int appendOutput;

} command_default = {0, NULL, NULL, NULL, 0};

typedef struct command_inter command;

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

internal_pair internals[] = {
    {"echo", internal_echo, 0},  // can run in child
    {"pwd", internal_pwd, 0},    // can run in child  
    {"cd", internal_cd, 1},      // MUST run in parent
    {"ls", internal_ls, 0},      // can run in child
    {"cat", internal_cat, 0},    // can run in child
    {NULL, NULL, 0}
};

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
    for (size_t i = 1; i < cmd->argc; ++i)
        printf("%s", cmd->argv[i]);
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
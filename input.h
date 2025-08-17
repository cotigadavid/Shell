#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>

#include "internalfuncs.h"

struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); // turn off echo & canonical mode
    //raw.c_lflag &= ~(ISIG);          // disable signals (Ctrl+C, Ctrl+Z)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

#define ARROW_UP    1000
#define ARROW_DOWN  1001
#define ARROW_RIGHT 1002
#define ARROW_LEFT  1003

int read_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;

    if (c == '\x1b') { // ESC sequence
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

#define HISTORY_MAX 100

char *history[HISTORY_MAX];
int history_len = 0;
int history_index = 0; // for navigation

void add_history(const char *cmd) {
    if (history_len < HISTORY_MAX) {
        history[history_len++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < HISTORY_MAX; i++)
            history[i-1] = history[i];
        history[HISTORY_MAX-1] = strdup(cmd);
    }
}

#define INPUT_BUF 1024

void redraw_prompt(const char *buf) {
    // Move cursor to start of line and clear it
    printf("\r\033[K");

    // Print your custom prompt
    printf("shell:~");
    internal_pwd(&command_default); // prints current working dir
    printf("$ ");

    // Print whatever is in the input buffer
    printf("%s", buf);

    fflush(stdout); // Make sure it appears immediately
}

void read_line(char *buf) {
    int pos = 0;
    buf[0] = '\0';
    history_index = history_len; // start at "new line" position

    printf("shell:~"); // 
    internal_pwd(&command_default); // 
    printf("$ "); // 
    fflush(stdout);

    while (1) {
        int key = read_key();

        if (key == '\n') { // Enter
            buf[pos] = '\0';
            printf("\n");
            return;
        } 
        else if (key == 127) { // Backspace
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                printf("\b \b"); // move back, print space, move back again
                fflush(stdout);
            }
        }
        else if (key == ARROW_UP) {
            if (history_index > 0) {
                history_index--;
                strcpy(buf, history[history_index]);
                pos = strlen(buf);
                redraw_prompt(buf); // clear line & print
                fflush(stdout);
            }
        }
        else if (key == ARROW_DOWN) {
            if (history_index < history_len) {
                history_index++;
                if (history_index == history_len)
                    buf[0] = '\0';
                else
                    strcpy(buf, history[history_index]);
                pos = strlen(buf);
                redraw_prompt(buf);
                fflush(stdout);
            }
        }
        else if (key >= 32 && key <= 126) { // printable characters
            if (pos < INPUT_BUF - 1) {
                buf[pos++] = key;
                buf[pos] = '\0';
                printf("%c", key);
                fflush(stdout);
            }
        }
    }
}
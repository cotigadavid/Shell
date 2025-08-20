#include "../headers/headers.h"
#include "../headers/proc.h"
#include "../headers/input.h"
#include "../headers/parser.h"
#include "../headers/variables.h"

extern command command_default = {0, NULL, NULL, NULL, 0, 0};
extern pipeline pipeline_default = {0, NULL, 0};

int main(void) {
    shell_tty = STDIN_FILENO;
    shell_interactive = isatty(shell_tty);

    if (shell_interactive) {
        shell_pgid = getpgrp();
        if (getpid() != shell_pgid) {
            if (setpgid(0, 0) < 0) {
                perror("setpgid");
                exit(1);
            }
            shell_pgid = getpgrp();
        }
        tcsetpgrp(shell_tty, shell_pgid);
    }

    load_environment();
    install_all_shell_handlers();
    enable_raw_mode();

    char input[INPUT_BUF];

    while (1) {
        check_child_status();

        read_line(input);

        // Trim newline
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n')
            input[len - 1] = '\0';

        // Skip empty lines
        if (input[0] == '\0') continue;

        add_history(input);

        if (strcmp(input, "exit") == 0)
            break;

        pipeline* curr_pipeline = parse_input(input, MAX_CMDS);
        if (curr_pipeline == NULL) continue;

        if (curr_pipeline->cmdc == 1) {
            execute_single_command(curr_pipeline);
        } else {
            execute_pipeline(curr_pipeline);
        }

        check_child_status();
        free_pipeline_mem(curr_pipeline);
    }

    return 0;
}

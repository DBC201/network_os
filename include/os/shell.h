#ifndef SHELL_H
#define SHELL_H

#include <sys/reboot.h>
#include <linux/reboot.h>

#include <iostream>
#include <cstring>
#include <unistd.h>

#include <string_utils.h>

using cpp_utils::string_utils::convert_string_vector;
using cpp_utils::string_utils::split;

void run_process(char** argv) {
    pid_t pid = fork();

    if (pid < 0) {
        std::perror("Unrecognized process");
    }
    else if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127); // regular exit is not async signal safe
    }
    else {
    }
}

void run_shell() {
    char buffer[4096];
    int received_size;

    while (1) {
        received_size = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (received_size < 0) {
            // TODO: handle stdin closure failure
            continue;
        }

        buffer[received_size] = '\0';

        if (received_size > 0 && buffer[received_size - 1] == '\n') {
            buffer[received_size - 1] = '\0';
        }

        if (std::strcmp(buffer, "shutdown") == 0) {
            reboot(LINUX_REBOOT_CMD_POWER_OFF);
        }
        else {
            std::string cmd(buffer);
            std::vector<std::string> tokens = split(cmd, ' ');

            if (tokens.empty()) {
                continue;
            }

            std::vector<char*> argv(tokens.size() + 1);

            for (int i=0; i<tokens.size(); i++) {
                argv[i] = tokens[i].data();
            }

            argv.back() = nullptr;
            run_process(argv.data());
        }

    }
}

#endif
#ifndef SHELL_H
#define SHELL_H

#include <sys/reboot.h>
#include <linux/reboot.h>

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string_utils.h>

using cpp_utils::string_utils::split;
using cpp_utils::string_utils::convert_string;

void run_process(char** argv) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Error creating fork.");
    }
    else if (pid == 0) {
        execvp(argv[0], argv);
        std::cerr << "Unrecognized process: " << argv[0] << std::endl;
        _exit(127); // regular exit is not async signal safe
    }
    else {
    }
}

void list_running_processes() {
    const std::string proc_dir = "/proc";
    std::vector<std::pair<int, std::string>> procs;

    for (const auto &entry : std::filesystem::directory_iterator(proc_dir)) {
        if (entry.is_directory()) {
            std::string dirname = entry.path().filename().string();
            int pid;

            try {
                pid = convert_string<int>(dirname);
            } catch (std::invalid_argument& e) {
                continue;
            }

            std::string cmdline_path = entry.path().string() + "/cmdline";
            std::ifstream cmdline_file(cmdline_path);
            std::string command_name;
            if (cmdline_file.is_open()) {
                std::getline(cmdline_file, command_name);
                if (!command_name.empty()) {
                    procs.push_back({pid, command_name});
                }
            }
            if (command_name.empty()) {
                std::string comm_path = entry.path().string() + "/comm";
                std::ifstream comm_file(comm_path);
                std::string process_name;
                if (comm_file.is_open()) {
                    std::getline(comm_file, process_name);
                    process_name.insert(process_name.begin(), '[');
                    process_name.push_back(']');
                    procs.push_back({pid, process_name});
                }
            }
        }
    }

    std::sort(procs.begin(), procs.end(), [](auto& a, auto& b) {
        return a.first < b.first;
    });

    std::cout << "PID COMMAND" << std::endl;

    for (const auto&p: procs) {
        std::cout << p.first << " " << p.second << std::endl;
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
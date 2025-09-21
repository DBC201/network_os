#ifndef SHELL_UTILS_H
#define SHELL_UTILS_H

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
#include <csignal>

#include <sys/wait.h>

using cpp_utils::string_utils::split;
using cpp_utils::string_utils::convert_string;

static void sigchld_handler(int) {
    // Reap as many as have exited
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;
    }
}

void init_reaper() {
    // Minimal SIGCHLD handler so parent reaps children
    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);
}

pid_t run_process(char** argv) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Error creating fork.");
    }
    else if (pid == 0) {
        execvp(argv[0], argv);
        std::cerr << "Unrecognized process: " << argv[0] << std::endl;
        _exit(127); // regular exit is not async signal safe
    }

    return pid;
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
                    std::replace(command_name.begin(), command_name.end(), '\0', ' ');
                    command_name.pop_back();
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

std::vector<std::pair<std::string,std::string>> parse_args(std::vector<std::string>& tokens) {
    std::vector<std::pair<std::string,std::string>> args = {{tokens[0], ""}};
    int i = 1;
    for (; i<tokens.size(); i++) {
        if (tokens[i].empty() || tokens[i][0] != '-') {
            break;
        }
        else if (i + 1 < tokens.size() && tokens[i+1][0] != '-') {
            args.push_back({tokens[i], tokens[i+1]});
            i++;
        }
        else {
            args.push_back({tokens[i], ""});
        }
    }

    if (args.size() > 1) {
        args.back().second = "";
        i--;
    }

    for (;i<tokens.size(); i++) {
        if (!tokens[i].empty() && tokens[i][0] == '-') {
            std::string error_message = "Error parsing " + tokens[i];
            throw std::invalid_argument(error_message);
        }
        else {
            args.push_back({tokens[i], ""});
        }
    }
    return args;
}

void run_shell() {
    init_reaper();

    char buffer[4096];
    int received_size;

    while (true) {
        received_size = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (received_size < 0) {
            std::cerr << "stdin closed unexpectedly" << std::endl;
            break;
        }

        buffer[received_size] = '\0';

        if (received_size > 0 && buffer[received_size - 1] == '\n') {
            buffer[received_size - 1] = '\0';
        }

        std::string cmd(buffer);
        std::vector<std::string> tokens = split(cmd, ' ');

        if (tokens.empty() || tokens[0].empty()) {
            continue;
        }
        else if (tokens[0] == "shutdown") {
            reboot(LINUX_REBOOT_CMD_POWER_OFF);
        }
        else if (tokens[0] == "kill") {
            std::vector<std::pair<std::string,std::string>> args;
            try {
                args = parse_args(tokens);
            } catch (std::invalid_argument& e) {
                std::cerr << "Usage: kill [-signal] <process_id>" << std::endl;
                continue;
            }

            if (args.size() != 2 && args.size() != 3) {
                std::cerr << "Usage: kill [-signal] <process_id>" << std::endl;
                continue;
            }

            int pid;
            int signal = 15;
            int r;

            try {
                pid = convert_string<int>(args.back().first);
            } catch (std::invalid_argument& e) {
                std::cerr << "Usage: kill [-signal] <process_id>" << std::endl;
                continue;
            }

            if (args.size() == 3 && (args[1].first.empty() || args[1].first[0] != '-')) {
                std::cerr << "Usage: kill [-signal] <process_id>" << std::endl;
                continue;
            }
            else if (args.size() == 3) {
                signal = convert_string<int>(args[1].first.substr(1));
            }

            r = kill(pid, signal);

            if (r == -1) {
                perror("Kill failed.");
            }
        }
        else {
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
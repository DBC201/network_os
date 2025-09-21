#include <os/init_utils.h>
#include <os/shell_utils.h>

int main() {
#ifndef DEBUG
    init_os();
#endif

    char* argv[] = { (char*)"shell", nullptr };
    pid_t shell_pid;

    while (true) {
        shell_pid = run_process(argv);
        if (shell_pid < 0) {
            std::cout << "Unable to start shell." << std::endl;
            emergency_shutdown();
            break;
        }
        
        int status;
        pid_t w = waitpid(shell_pid, &status, 0);

        if (w == -1) {
            perror("Error waiting for shell");
            emergency_shutdown();
            break;
        } else {
            std::cout << "Shell stopped with status: " << std::hex << status << std::dec << std::endl;
        }

        std::cerr << "Restarting shell..." << std::endl;
        sleep(1);
    }

    return 1;
}

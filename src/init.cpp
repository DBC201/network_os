#include <os/init_utils.h>
#include <os/shell_utils.h>

int main() {
    init_os();

    char* shell_argv[] = { (char*)"shell", nullptr };
    char* device_manager_argv[] = { (char*)"device_manager", (char*)"devman", (char*)"fwd", nullptr };
    char* forwarder_argv[] = { (char*)"forwarder", (char*)"fwd", nullptr };
    
    pid_t shell_pid;
    
    pid_t device_manager_pid = run_process(device_manager_argv);

    if (device_manager_pid < 0) {
        std::cerr << "Unable to start device_manager." << std::endl;
    }

    pid_t forwarder_pid = run_process(forwarder_argv);

    if (forwarder_pid < 0) {
        std::cerr << "Unable to start forwarder." << std::endl;
    }

    while (true) {
        shell_pid = run_process(shell_argv);
        if (shell_pid < 0) {
            std::cerr << "Unable to start shell." << std::endl;
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
            std::cerr << "Shell stopped with status: " << std::hex << status << std::dec << std::endl;
        }

        std::cerr << "Restarting shell..." << std::endl;
        sleep(1);
    }

    return 1;
}

#include <os/init_utils.h>
#include <os/shell.h>

int main() {
    if (!DEBUG) {
        init_os();
    }

    run_shell();

    return -1;
}

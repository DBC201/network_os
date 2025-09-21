#ifndef INIT_UTILS
#define INIT_UTILS

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <iostream>
#include <linux/reboot.h>
#include <sys/reboot.h>

#include "shell_utils.h"

static void set_umask_zero() {
    umask(0);
}

static bool mkdir_p(const char* path, mode_t mode = 0755) {
    if (mkdir(path, mode) == 0) return true;
    if (errno == EEXIST)       return true;
    return false;
}

static void redirect_stdio_to_console() {
    int fd = -1;

    // Keep trying until /dev/console exists
    while (true) {
        fd = open("/dev/console", O_RDWR);
        if (fd >= 0) break;

        // Optional: log to kernel log buffer
        dprintf(STDERR_FILENO, "[init] waiting for /dev/console: %s\n", strerror(errno));

        // Back off a little
        sleep(1);
    }

    // Redirect stdio
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO) close(fd);
}

static void mount_fs(const char* src, const char* target, const char* fstype,
                     unsigned long flags = 0, const char* data = nullptr) {
    if (mount(src, target, fstype, flags, data) < 0) {
        char buf[256];
        const char* err = strerror_r(errno, buf, sizeof(buf)) ? "mount error" : buf;
        write(STDERR_FILENO, "[init] mount failed: ", 21);

        // TODO: handle mount failure
    }
}

void init_os() {
    umask(0);

    init_reaper();

    // PATH for any helper binaries inside initramfs
    setenv("PATH", "/bin:/sbin", 1);

    // Ensure mount points exist
    if (!mkdir_p("/dev") || !mkdir_p("/proc") || !mkdir_p("/sys")) {
        // TODO: handle mkdir failure
    }

    // If your kernel does NOT have CONFIG_DEVTMPFS_MOUNT=y, uncomment:
    // mount_fs("devtmpfs", "/dev", "devtmpfs", MS_NOSUID|MS_NOEXEC, "mode=0755");

    // Attach stdio to the kernel console early
    redirect_stdio_to_console();

    // Mount proc and sysfs
    mount_fs("proc",  "/proc", "proc",  MS_NOSUID|MS_NOEXEC|MS_NODEV);
    mount_fs("sysfs", "/sys",  "sysfs", MS_NOSUID|MS_NOEXEC|MS_NODEV);
}

void emergency_shutdown() {
    std::cerr << "Attempting shutdown to prevent kernel panic." << std::endl;
    std::cerr << "Press Enter to shutdown immediately." << std::endl;
    std::cerr << "System will shut down in 10 seconds." << std::endl;

    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 10000);

    if (ret > 0) {
        char buf[1];
        read(STDIN_FILENO, buf, 1);
        std::cerr << "Shutting down now (user input).\n";
    } else if (ret == 0) {
        std::cerr << "No input detected, shutting down after 10s timeout.\n";
    } else {
        perror("poll");
        sleep(10);
    }
    
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
}

#endif

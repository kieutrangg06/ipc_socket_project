#ifndef LOGGER_H
#define LOGGER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

#define LOG_FILE "server.log"

// Production-grade Double-Fork daemonization sequence
static inline void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Parent 1 exits, returning shell prompt

    if (setsid() < 0) exit(EXIT_FAILURE); // Detach from controlling TTY and create new session

    pid = fork(); // Second fork: guarantees process can never reacquire a TTY
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Session leader terminates

    umask(0);

    // Redirect standard I/O descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

// Process-safe audit logging protected by fcntl() advisory record locks
static inline void write_server_log(const char *message) {
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK; // Exclusive Write Lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;        // Lock through EOF

    if (fcntl(fd, F_SETLKW, &fl) != -1) {
        time_t now = time(NULL);
        char time_buf[64];
        struct tm *tm_info = localtime(&now);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        dprintf(fd, "[%s] %s\n", time_buf, message);

        fl.l_type = F_UNLCK; // Explicitly release lock
        fcntl(fd, F_SETLK, &fl);
    }

    close(fd);
}

#endif

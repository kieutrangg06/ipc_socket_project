#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include "protocol.h"

typedef struct {
    int fd;
    char name[NAME_LEN];
    int active;
} ClientContext;

static ClientContext clients[MAX_CLIENTS];
static struct pollfd fds[MAX_CLIENTS + 1];
static int server_fd = -1;

void clean_up(int sig) {
    (void)sig;
    printf("\n" ANSI_YELLOW "[SERVER] Đang dọn dẹp tài nguyên và tắt Server..." ANSI_RESET "\n");
    if (server_fd != -1) close(server_fd);
    unlink(SOCKET_PATH);
    exit(0);
}

int read_all(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *ptr = (char *)buf;
    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return n;
        }
        total += n;
    }
    return total;
}

int send_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *ptr = (const char *)buf;
    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return n;
        }
        total += n;
    }
    return total;
}

void broadcast_system(const char *msg) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MSG_SYSTEM_NOTIFY;
    hdr.payload_len = strlen(msg);
    strncpy(hdr.sender, "SERVER", NAME_LEN - 1);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            send_all(clients[i].fd, &hdr, sizeof(hdr));
            send_all(clients[i].fd, msg, hdr.payload_len);
        }
    }
}

int main() {
    signal(SIGINT, clean_up);
    signal(SIGPIPE, SIG_IGN);

    unlink(SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Tạo Socket thất bại");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind thất bại");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen thất bại");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf(ANSI_GREEN "==========================================================" ANSI_RESET "\n");
    printf(ANSI_GREEN "[SERVER] IPC Socket Server đang chạy tại: %s" ANSI_RESET "\n", SOCKET_PATH);
    printf(ANSI_GREEN "==========================================================" ANSI_RESET "\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].active = 0;
    }

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (1) {
        int nfds = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                fds[nfds].fd = clients[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        int poll_res = poll(fds, nfds, -1);
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            perror("Poll error");
            break;
        }

        if (fds[0].revents & POLLIN) {
            int new_fd = accept(server_fd, NULL, NULL);
            if (new_fd >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clients[i].active) {
                        slot = i;
                        break;
                    }
                }
                if (slot != -1) {
                    clients[slot].fd = new_fd;
                    clients[slot].active = 1;
                    snprintf(clients[slot].name, NAME_LEN, "User%d", new_fd);
                    printf(ANSI_CYAN "[SERVER] FD %d kết nối thành công." ANSI_RESET "\n", new_fd);
                } else {
                    printf(ANSI_RED "[SERVER] Quá tải client, từ chối kết nối!" ANSI_RESET "\n");
                    close(new_fd);
                }
            }
        }

        int cur_idx = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) continue;

            if (fds[cur_idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                PacketHeader hdr;
                int n = read_all(clients[i].fd, &hdr, sizeof(hdr));
                
                if (n <= 0) {
                    char leave_msg[128];
                    snprintf(leave_msg, sizeof(leave_msg), "Thành viên [%s] đã rời phòng.", clients[i].name);
                    printf(ANSI_YELLOW "[SERVER] %s" ANSI_RESET "\n", leave_msg);
                    close(clients[i].fd);
                    clients[i].active = 0;
                    broadcast_system(leave_msg);
                } else {
                    char *payload = NULL;
                    if (hdr.payload_len > 0) {
                        payload = (char *)malloc(hdr.payload_len);
                        read_all(clients[i].fd, payload, hdr.payload_len);
                    }

                    if (hdr.type == MSG_LOGIN) {
                        strncpy(clients[i].name, hdr.sender, NAME_LEN - 1);
                        char join_msg[128];
                        snprintf(join_msg, sizeof(join_msg), "Chào mừng [%s] tham gia hệ thống!", clients[i].name);
                        printf(ANSI_CYAN "[SERVER] %s" ANSI_RESET "\n", join_msg);
                        broadcast_system(join_msg);
                    } 
                    else if (hdr.type == MSG_CHAT_PUBLIC) {
                        printf(ANSI_GREEN "[PUBLIC CHAT] [%s]: %.*s" ANSI_RESET "\n", 
                               hdr.sender, hdr.payload_len, payload);
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].active && clients[j].fd != clients[i].fd) {
                                send_all(clients[j].fd, &hdr, sizeof(hdr));
                                send_all(clients[j].fd, payload, hdr.payload_len);
                            }
                        }
                    } 
                    else if (hdr.type == MSG_CHAT_PRIVATE || hdr.type == MSG_FILE_START || 
                             hdr.type == MSG_FILE_DATA || hdr.type == MSG_FILE_END) {
                        int sent = 0;
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].active && strcmp(clients[j].name, hdr.target) == 0) {
                                send_all(clients[j].fd, &hdr, sizeof(hdr));
                                if (hdr.payload_len > 0) {
                                    send_all(clients[j].fd, payload, hdr.payload_len);
                                }
                                sent = 1;
                                break;
                            }
                        }
                        if (!sent && hdr.type == MSG_CHAT_PRIVATE) {
                            char err_msg[128];
                            snprintf(err_msg, sizeof(err_msg), "Không tìm thấy người dùng [%s].", hdr.target);
                            PacketHeader err_hdr;
                            memset(&err_hdr, 0, sizeof(err_hdr));
                            err_hdr.type = MSG_SYSTEM_NOTIFY;
                            err_hdr.payload_len = strlen(err_msg);
                            strncpy(err_hdr.sender, "SERVER", NAME_LEN - 1);
                            send_all(clients[i].fd, &err_hdr, sizeof(err_hdr));
                            send_all(clients[i].fd, err_msg, err_hdr.payload_len);
                        }
                    }

                    if (payload) free(payload);
                }
            }
            cur_idx++;
        }
    }

    clean_up(0);
    return 0;
}

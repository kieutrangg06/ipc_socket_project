#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include "logger.h"

typedef struct {
    int fd;
    char name[NAME_LEN];
    int active;
} ClientContext;

static ClientContext clients[MAX_CLIENTS];
static struct pollfd fds[MAX_CLIENTS + 1];
static int server_fd = -1;

void server_log(const char *level, const char *msg) {
    printf(ANSI_BLUE "[%s] " ANSI_RESET "%s\n", level, msg);
    char buf[512];
    snprintf(buf, sizeof(buf), "[%s] %s", level, msg);
    write_server_log(buf);
}

void clean_up(int sig) {
    (void)sig;
    write_server_log("[SERVER] Dọn dẹp tài nguyên và tắt Server.");
    printf("\n" ANSI_YELLOW "[SERVER] Đang dọn dẹp tài nguyên và tắt Server..." ANSI_RESET "\n");
    if (server_fd != -1) close(server_fd);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd != -1) {
            close(clients[i].fd);
            clients[i].fd = -1;
            clients[i].active = 0;
        }
    }
    unlink(SOCKET_PATH);
    exit(0);
}

void broadcast_system(const char *msg) {
    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MSG_SYSTEM_NOTIFY;
    hdr.payload_len = strlen(msg);
    snprintf(hdr.sender, sizeof(hdr.sender), "SERVER");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            send_all(clients[i].fd, &hdr, sizeof(hdr));
            send_all(clients[i].fd, msg, hdr.payload_len);
        }
    }
}

// Bỏ qua phần payload lỗi để bảo vệ tính đồng bộ Framing của luồng Socket
static void drain_socket(int fd, size_t len) {
    char junk[1024];
    while (len > 0) {
        size_t to_read = (len < sizeof(junk)) ? len : sizeof(junk);
        int r = read_all(fd, junk, to_read);
        if (r <= 0) break;
        len -= r;
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemonize();
    }

    write_server_log("Server đã khởi động thành công!");
    signal(SIGINT, clean_up);
    signal(SIGTERM, clean_up);
    signal(SIGPIPE, SIG_IGN); // Bỏ qua SIGPIPE tránh crash tiến trình khi ghi vào socket đã đóng

    unlink(SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Tạo Socket thất bại");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

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
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "IPC Socket Server đang chạy tại: %s", SOCKET_PATH);
    server_log("SERVER", start_msg);
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

        // Chấp nhận kết nối mới
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

                    char connect_msg[128];
                    snprintf(connect_msg, sizeof(connect_msg), "Client FD %d đã kết nối.", new_fd);
                    server_log("CONNECT", connect_msg);
                } else {
                    server_log("SERVER", "Đã đạt giới hạn Client, từ chối kết nối!");
                    close(new_fd);
                }
            }
        }

        // Xử lý sự kiện từ các Client
        int cur_idx = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) continue;

            if (fds[cur_idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                PacketHeader hdr;
                int n = read_all(clients[i].fd, &hdr, sizeof(hdr));

                if (n <= 0) {
                    char leave_msg[128];
                    snprintf(leave_msg, sizeof(leave_msg), "Thành viên [%s] đã rời phòng.", clients[i].name);
                    server_log("DISCONNECT", leave_msg);

                    close(clients[i].fd);
                    clients[i].active = 0;
                    broadcast_system(leave_msg);
                } else {
                    char *payload = NULL;
                    if (hdr.payload_len > 0) {
                        if (hdr.payload_len > MAX_PAYLOAD_SIZE) {
                            drain_socket(clients[i].fd, hdr.payload_len);
                            cur_idx++;
                            continue;
                        }
                        payload = (char *)malloc(hdr.payload_len);
                        if (payload) {
                            read_all(clients[i].fd, payload, hdr.payload_len);
                        } else {
                            drain_socket(clients[i].fd, hdr.payload_len);
                            cur_idx++;
                            continue;
                        }
                    }

                    if (hdr.type == MSG_LOGIN) {
                        // Kiểm tra trùng Nickname
                        int duplicate = 0;
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].active && j != i && strcmp(clients[j].name, hdr.sender) == 0) {
                                duplicate = 1;
                                break;
                            }
                        }

                        if (duplicate) {
                            // Tự động thêm hậu tố phân biệt nếu bị trùng
                           snprintf(clients[i].name, sizeof(clients[i].name), "%.20s_%d", hdr.sender, clients[i].fd);
                        } else {
                            snprintf(clients[i].name, sizeof(clients[i].name), "%s", hdr.sender);
                        }

                        char join_msg[128];
                        snprintf(join_msg, sizeof(join_msg), "Chào mừng [%s] tham gia hệ thống!", clients[i].name);
                        server_log("LOGIN", join_msg);
                        broadcast_system(join_msg);
                    }
                    else if (hdr.type == MSG_CHAT_PUBLIC) {
                        char public_msg[BUFFER_SIZE + 64];
                        snprintf(public_msg, sizeof(public_msg), "[%s]: %.*s", hdr.sender, (int)hdr.payload_len, payload);
                        server_log("PUBLIC", public_msg);

                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].active && clients[j].fd != clients[i].fd) {
                                send_all(clients[j].fd, &hdr, sizeof(hdr));
                                send_all(clients[j].fd, payload, hdr.payload_len);
                            }
                        }
                    }
                    else if (hdr.type == MSG_CHAT_PRIVATE ||
                             hdr.type == MSG_FILE_START ||
                             hdr.type == MSG_FILE_DATA ||
                             hdr.type == MSG_FILE_END) {

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
                            snprintf(err_hdr.sender, sizeof(err_hdr.sender), "SERVER");

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <sys/stat.h>
#include "protocol.h"

static int sock_fd = -1;
static char my_name[NAME_LEN];
static FILE *rx_file = NULL;
static uint64_t rx_filesize = 0;
static uint64_t rx_received = 0;
static char rx_filename[64];

void draw_progress_bar(uint64_t current, uint64_t total) {
    int bar_width = 30;
    float progress = (total == 0) ? 1.0f : (float)current / total;
    int pos = (int)(bar_width * progress);

    printf("\r" ANSI_MAGENTA "Tiến trình tải: [" ANSI_RESET);
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf(ANSI_GREEN "=" ANSI_RESET);
        else if (i == pos) printf(ANSI_GREEN ">" ANSI_RESET);
        else printf(" ");
    }
    printf(ANSI_MAGENTA "] %3.0f%% (%lu/%lu bytes)" ANSI_RESET, progress * 100.0, current, total);
    fflush(stdout);
    if (current >= total) printf("\n");
}

void send_file(const char *target, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf(ANSI_RED "[LỖI] Không thể mở file: %s" ANSI_RESET "\n", filepath);
        return;
    }

    struct stat st;
    stat(filepath, &st);
    uint64_t total_size = st.st_size;

    const char *base_filename = strrchr(filepath, '/');
    base_filename = (base_filename) ? base_filename + 1 : filepath;

    FileInfoPayload finfo;
    memset(&finfo, 0, sizeof(finfo));
    strncpy(finfo.filename, base_filename, sizeof(finfo.filename) - 1);
    finfo.filesize = total_size;

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MSG_FILE_START;
    hdr.payload_len = sizeof(finfo);
    strncpy(hdr.sender, my_name, NAME_LEN - 1);
    strncpy(hdr.target, target, NAME_LEN - 1);

    send_all(sock_fd, &hdr, sizeof(hdr));
    send_all(sock_fd, &finfo, sizeof(finfo));

    printf(ANSI_CYAN "[GỬI FILE] Bắt đầu gửi file '%s' (%lu bytes) tới [%s]..." ANSI_RESET "\n", 
           base_filename, total_size, target);

    char buffer[2048];
    size_t bytes_read = 0;
    uint64_t total_sent = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        hdr.type = MSG_FILE_DATA;
        hdr.payload_len = bytes_read;
        send_all(sock_fd, &hdr, sizeof(hdr));
        send_all(sock_fd, buffer, bytes_read);

        total_sent += bytes_read;
        draw_progress_bar(total_sent, total_size);
        usleep(500);
    }
    fclose(f);

    hdr.type = MSG_FILE_END;
    hdr.payload_len = 0;
    send_all(sock_fd, &hdr, sizeof(hdr));

    printf(ANSI_GREEN "[GỬI FILE] Đã gửi file thành công!" ANSI_RESET "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Sử dụng: %s <Nickname>\n", argv[0]);
        return 1;
    }
    strncpy(my_name, argv[1], NAME_LEN - 1);

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Tạo Socket lỗi");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Kết nối tới Server thất bại (Server đã bật chưa?)");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    PacketHeader login_hdr;
    memset(&login_hdr, 0, sizeof(login_hdr));
    login_hdr.type = MSG_LOGIN;
    login_hdr.payload_len = 0;
    strncpy(login_hdr.sender, my_name, NAME_LEN - 1);
    send_all(sock_fd, &login_hdr, sizeof(login_hdr));

    printf(ANSI_GREEN "Đã kết nối thành công với Nickname: %s" ANSI_RESET "\n", my_name);
    printf("Hướng dẫn:\n");
    printf(" - Chat thông thường: Nhập nội dung rồi nhấn Enter\n");
    printf(" - Chat riêng: " ANSI_YELLOW "/msg <tên> <nội dung>" ANSI_RESET "\n");
    printf(" - Gửi file:   " ANSI_YELLOW "/sendfile <tên> <đường_dẫn_file>" ANSI_RESET "\n\n");

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock_fd;
    pfds[1].events = POLLIN;

    char line_buf[BUFFER_SIZE];

    while (1) {
        int ret = poll(pfds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (pfds[0].revents & POLLIN) {
            if (fgets(line_buf, sizeof(line_buf), stdin) == NULL) break;
            line_buf[strcspn(line_buf, "\n")] = 0;

            if (strlen(line_buf) == 0) continue;

            if (strncmp(line_buf, "/msg ", 5) == 0) {
                char target[NAME_LEN];
                char msg[BUFFER_SIZE];
                if (sscanf(line_buf + 5, "%s %[^\n]", target, msg) == 2) {
                    PacketHeader hdr;
                    memset(&hdr, 0, sizeof(hdr));
                    hdr.type = MSG_CHAT_PRIVATE;
                    hdr.payload_len = strlen(msg);
                    strncpy(hdr.sender, my_name, NAME_LEN - 1);
                    strncpy(hdr.target, target, NAME_LEN - 1);

                    send_all(sock_fd, &hdr, sizeof(hdr));
                    send_all(sock_fd, msg, hdr.payload_len);
                    printf(ANSI_MAGENTA "[Bạn -> %s]: %s" ANSI_RESET "\n", target, msg);
                } else {
                    printf(ANSI_RED "Sai cú pháp! Dùng: /msg <tên> <nội dung>" ANSI_RESET "\n");
                }
            } 
            else if (strncmp(line_buf, "/sendfile ", 10) == 0) {
                char target[NAME_LEN];
                char filepath[256];
                if (sscanf(line_buf + 10, "%s %s", target, filepath) == 2) {
                    send_file(target, filepath);
                } else {
                    printf(ANSI_RED "Sai cú pháp! Dùng: /sendfile <tên> <đường_dẫn_file>" ANSI_RESET "\n");
                }
            } 
            else {
                PacketHeader hdr;
                memset(&hdr, 0, sizeof(hdr));
                hdr.type = MSG_CHAT_PUBLIC;
                hdr.payload_len = strlen(line_buf);
                strncpy(hdr.sender, my_name, NAME_LEN - 1);

                send_all(sock_fd, &hdr, sizeof(hdr));
                send_all(sock_fd, line_buf, hdr.payload_len);
            }
        }

        if (pfds[1].revents & POLLIN) {
            PacketHeader hdr;
            int n = read_all(sock_fd, &hdr, sizeof(hdr));
            if (n <= 0) {
                printf("\n" ANSI_RED "[MẤT KẾT NỐI] Server đã dừng hoạt động." ANSI_RESET "\n");
                break;
            }

            char *payload = NULL;
            if (hdr.payload_len > 0) {
                payload = (char *)malloc(hdr.payload_len + 1);
                read_all(sock_fd, payload, hdr.payload_len);
                payload[hdr.payload_len] = '\0';
            }

            if (hdr.type == MSG_SYSTEM_NOTIFY) {
                printf(ANSI_YELLOW "[HỆ THỐNG] %s" ANSI_RESET "\n", payload);
            } 
            else if (hdr.type == MSG_CHAT_PUBLIC) {
                printf(ANSI_GREEN "[%s]:" ANSI_RESET " %s\n", hdr.sender, payload);
            } 
            else if (hdr.type == MSG_CHAT_PRIVATE) {
                printf(ANSI_MAGENTA "[RIÊNG từ %s]: %s" ANSI_RESET "\n", hdr.sender, payload);
            } 
            else if (hdr.type == MSG_FILE_START) {
                FileInfoPayload *fi = (FileInfoPayload *)payload;
                rx_filesize = fi->filesize;
                rx_received = 0;
                snprintf(rx_filename, sizeof(rx_filename), "nhan_%s", fi->filename);
                rx_file = fopen(rx_filename, "wb");
                printf("\n" ANSI_CYAN "[FILE NHẬN] Đang nhận file '%s' (%lu bytes) từ [%s]..." ANSI_RESET "\n", 
                       fi->filename, rx_filesize, hdr.sender);
            } 
            else if (hdr.type == MSG_FILE_DATA) {
                if (rx_file) {
                    fwrite(payload, 1, hdr.payload_len, rx_file);
                    rx_received += hdr.payload_len;
                    draw_progress_bar(rx_received, rx_filesize);
                }
            } 
            else if (hdr.type == MSG_FILE_END) {
                if (rx_file) {
                    fclose(rx_file);
                    rx_file = NULL;
                    printf(ANSI_GREEN "[HOÀN TẤT] File đã lưu thành: %s" ANSI_RESET "\n", rx_filename);
                }
            }

            if (payload) free(payload);
        }
    }

    close(sock_fd);
    return 0;
}

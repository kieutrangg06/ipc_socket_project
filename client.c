#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
static char rx_filename[128];
static int raw_mode = 0;

void draw_progress_bar(uint64_t current, uint64_t total) {
    int bar_width = 30;
    float progress = (total == 0) ? 1.0f : (float)current / total;
    int pos = (int)(bar_width * progress);

    printf("\r" ANSI_MAGENTA "Progress: [" ANSI_RESET);
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
        printf(ANSI_RED "[ERROR] Cannot open file: %s" ANSI_RESET "\n", filepath);
        return;
    }

    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("Stat file error");
        fclose(f);
        return;
    }
    uint64_t total_size = st.st_size;

    const char *base_filename = strrchr(filepath, '/');
    base_filename = (base_filename) ? base_filename + 1 : filepath;

    FileInfoPayload finfo;
    memset(&finfo, 0, sizeof(finfo));
    snprintf(finfo.filename, sizeof(finfo.filename), "%s", base_filename);
    finfo.filesize = total_size;

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MSG_FILE_START;
    hdr.payload_len = sizeof(finfo);
    snprintf(hdr.sender, sizeof(hdr.sender), "%s", my_name);
    snprintf(hdr.target, sizeof(hdr.target), "%s", target);

    send_all(sock_fd, &hdr, sizeof(hdr));
    send_all(sock_fd, &finfo, sizeof(finfo));

    printf(ANSI_CYAN "[FILE TRANSFER] Sending file '%s' (%lu bytes) to [%s]..." ANSI_RESET "\n", 
           base_filename, total_size, target);

    char buffer[CHUNK_SIZE];
    size_t bytes_read = 0;
    uint64_t total_sent = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        hdr.type = MSG_FILE_DATA;
        hdr.payload_len = bytes_read;
        send_all(sock_fd, &hdr, sizeof(hdr));
        send_all(sock_fd, buffer, bytes_read);

        total_sent += bytes_read;
        draw_progress_bar(total_sent, total_size);
        if (!raw_mode) {
            usleep(400); // Visual pacing: throttles rendering speed for smooth ANSI display
        }
    }
    fclose(f);

    hdr.type = MSG_FILE_END;
    hdr.payload_len = 0;
    send_all(sock_fd, &hdr, sizeof(hdr));

    printf(ANSI_GREEN "[FILE TRANSFER] File sent successfully!" ANSI_RESET "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <Nickname> [--raw]\n", argv[0]);
        return 1;
    }
    snprintf(my_name, sizeof(my_name), "%s", argv[1]);
    if (argc > 2 && strcmp(argv[2], "--raw") == 0) {
        raw_mode = 1;
    }
    if (getenv("RAW_MODE") != NULL && strcmp(getenv("RAW_MODE"), "1") == 0) {
        raw_mode = 1;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection to Server failed (Is server running?)");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    PacketHeader login_hdr;
    memset(&login_hdr, 0, sizeof(login_hdr));
    login_hdr.type = MSG_LOGIN;
    login_hdr.payload_len = 0;
    snprintf(login_hdr.sender, sizeof(login_hdr.sender), "%s", my_name);
    send_all(sock_fd, &login_hdr, sizeof(login_hdr));

    printf(ANSI_GREEN "Connected successfully with Nickname: %s" ANSI_RESET "\n", my_name);
    printf("Commands:\n");
    printf(" - Public chat:  Type message and press Enter\n");
    printf(" - Private chat: " ANSI_YELLOW "/msg <target> <message>" ANSI_RESET "\n");
    printf(" - Send file:    " ANSI_YELLOW "/sendfile <target> <file_path>" ANSI_RESET "\n\n");

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
                // Width specifier prevents buffer overflow attacks
                if (sscanf(line_buf + 5, "%31s %4095[^\n]", target, msg) == 2) {
                    PacketHeader hdr;
                    memset(&hdr, 0, sizeof(hdr));
                    hdr.type = MSG_CHAT_PRIVATE;
                    hdr.payload_len = strlen(msg);
                    snprintf(hdr.sender, sizeof(hdr.sender), "%s", my_name);
                    snprintf(hdr.target, sizeof(hdr.target), "%s", target);

                    send_all(sock_fd, &hdr, sizeof(hdr));
                    send_all(sock_fd, msg, hdr.payload_len);
                    printf(ANSI_MAGENTA "[You -> %s]: %s" ANSI_RESET "\n", target, msg);
                } else {
                    printf(ANSI_RED "Syntax error! Use: /msg <target> <message>" ANSI_RESET "\n");
                }
            } 
            else if (strncmp(line_buf, "/sendfile ", 10) == 0) {
                char target[NAME_LEN];
                char filepath[256];
                if (sscanf(line_buf + 10, "%31s %255s", target, filepath) == 2) {
                    send_file(target, filepath);
                } else {
                    printf(ANSI_RED "Syntax error! Use: /sendfile <target> <file_path>" ANSI_RESET "\n");
                }
            } 
            else {
                PacketHeader hdr;
                memset(&hdr, 0, sizeof(hdr));
                hdr.type = MSG_CHAT_PUBLIC;
                hdr.payload_len = strlen(line_buf);
                snprintf(hdr.sender, sizeof(hdr.sender), "%s", my_name);

                send_all(sock_fd, &hdr, sizeof(hdr));
                send_all(sock_fd, line_buf, hdr.payload_len);
            }
        }

        if (pfds[1].revents & POLLIN) {
            PacketHeader hdr;
            int n = read_all(sock_fd, &hdr, sizeof(hdr));
            if (n <= 0) {
                printf("\n" ANSI_RED "[DISCONNECTED] Server stopped." ANSI_RESET "\n");
                break;
            }

            char *payload = NULL;
            if (hdr.payload_len > 0) {
                payload = (char *)malloc(hdr.payload_len + 1);
                if (payload) {
                    read_all(sock_fd, payload, hdr.payload_len);
                    payload[hdr.payload_len] = '\0';
                }
            }

            if (hdr.type == MSG_SYSTEM_NOTIFY && payload) {
                printf(ANSI_YELLOW "[SYSTEM] %s" ANSI_RESET "\n", payload);
            } 
            else if (hdr.type == MSG_CHAT_PUBLIC && payload) {
                printf(ANSI_GREEN "[%s]:" ANSI_RESET " %s\n", hdr.sender, payload);
            } 
            else if (hdr.type == MSG_CHAT_PRIVATE && payload) {
                printf(ANSI_MAGENTA "[PRIVATE from %s]: %s" ANSI_RESET "\n", hdr.sender, payload);
            } 
            else if (hdr.type == MSG_FILE_START && payload) {
                FileInfoPayload *fi = (FileInfoPayload *)payload;
                rx_filesize = fi->filesize;
                rx_received = 0;
                
                // Defend against Path Traversal vulnerabilities
                const char *clean_fname = strrchr(fi->filename, '/');
                clean_fname = (clean_fname) ? clean_fname + 1 : fi->filename;

                snprintf(rx_filename, sizeof(rx_filename), "received_%s", clean_fname);
                if (rx_file) {
                    fclose(rx_file);
                    rx_file = NULL;
                }
                rx_file = fopen(rx_filename, "wb");
                printf("\n" ANSI_CYAN "[FILE RECEIVE] Receiving file '%s' (%lu bytes) from [%s]..." ANSI_RESET "\n", 
                       clean_fname, rx_filesize, hdr.sender);
            } 
            else if (hdr.type == MSG_FILE_DATA && payload) {
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
                    printf(ANSI_GREEN "[COMPLETED] File saved to: %s" ANSI_RESET "\n", rx_filename);
                }
            }

            if (payload) free(payload);
        }
    }

    if (rx_file) {
        fclose(rx_file);
        rx_file = NULL;
    }
    close(sock_fd);
    return 0;
}

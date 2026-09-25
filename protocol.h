#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define SOCKET_PATH "/tmp/sys_ipc_socket.sock"
#define DEFAULT_PORT 8888
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 32
#define NAME_LEN 32

/* Loại gói tin */
typedef enum {
    MSG_LOGIN = 1,        // Đăng ký Nickname
    MSG_CHAT_PUBLIC,      // Chat chung
    MSG_CHAT_PRIVATE,     // Chat riêng (/msg <user> <text>)
    MSG_FILE_START,       // Bắt đầu gửi file (metadata: tên file, kích thước)
    MSG_FILE_DATA,        // Khối dữ liệu file (chunk data)
    MSG_FILE_END,         // Báo hiệu kết thúc file
    MSG_SYSTEM_NOTIFY     // Thông báo hệ thống từ Server
} MessageType;

/* Cấu trúc Header cố định: 76 bytes */
#pragma pack(push, 1)
typedef struct {
    uint32_t type;            // Loại gói tin (MessageType)
    uint32_t payload_len;     // Kích thước phần dữ liệu đính kèm phía sau
    char sender[NAME_LEN];    // Tên người gửi
    char target[NAME_LEN];    // Tên người nhận (dùng cho private/file)
} PacketHeader;
#pragma pack(pop)

/* Header phụ chứa thông tin file khi bắt đầu gửi */
#pragma pack(push, 1)
typedef struct {
    char filename[64];
    uint64_t filesize;
} FileInfoPayload;
#pragma pack(pop)

/* Màu sắc ANSI cho Terminal */
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[1;31m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_BLUE    "\033[1;34m"
#define ANSI_MAGENTA "\033[1;35m"
#define ANSI_CYAN    "\033[1;36m"

#endif

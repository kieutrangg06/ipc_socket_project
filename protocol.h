#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#define SOCKET_PATH "/tmp/ipc_chat_system.sock"
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 32
#define NAME_LEN 32
#define CHUNK_SIZE 2048
#define MAX_PAYLOAD_SIZE (10 * 1024 * 1024) // Maximum payload limit: 10MB to prevent memory exhaustion (Anti-DoS)

typedef enum {
    MSG_LOGIN = 1,
    MSG_CHAT_PUBLIC,
    MSG_CHAT_PRIVATE,
    MSG_FILE_START,
    MSG_FILE_DATA,
    MSG_FILE_END,
    MSG_SYSTEM_NOTIFY
} MessageType;

#pragma pack(push, 1)
typedef struct {
    uint32_t type;         // 4 bytes: MessageType opcode (1 - 7)
    uint32_t payload_len;  // 4 bytes: Length of trailing payload
    char sender[NAME_LEN]; // 32 bytes: Sender nickname
    char target[NAME_LEN]; // 32 bytes: Target recipient nickname
} PacketHeader;            // Exactly 72 bytes packed

typedef struct {
    char filename[64];     // 64 bytes: Base filename
    uint64_t filesize;     // 8 bytes: Total byte length
} FileInfoPayload;         // Exactly 72 bytes packed
#pragma pack(pop)

#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[1;31m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_BLUE    "\033[1;34m"
#define ANSI_MAGENTA "\033[1;35m"
#define ANSI_CYAN    "\033[1;36m"

// Atomic read primitive: loops until all requested bytes are committed
static inline int read_all(int fd, void *buf, size_t count) {
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
    return (int)total;
}

// Atomic send primitive: handles signal interruptions and partial writes
static inline int send_all(int fd, const void *buf, size_t count) {
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
    return (int)total;
}

#endif

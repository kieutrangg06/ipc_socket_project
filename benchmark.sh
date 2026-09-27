
#!/bin/bash

# Benchmark file transfer cho IPC Socket Project
# Member 4 - Client, File Transfer & Benchmark

set -u

cd "$(dirname "$0")" || exit 1

SIZES=(10 50 100)
SENDER_LOG="benchmark_sender.log"
RECEIVER_LOG="benchmark_receiver.log"
RESULTS="benchmark_results.csv"

SENDER_FIFO="benchmark_sender.in"
RECEIVER_FIFO="benchmark_receiver.in"

# Kiem tra server dang chay
if [ ! -S /tmp/sys_ipc_socket.sock ]; then
    echo "ERROR: Server chua chay!"
    echo "Hay khoi dong ./server trong Terminal 1."
    exit 1
fi

# Kiem tra cac file test
for size in "${SIZES[@]}"; do
    if [ ! -f "test_${size}MB.bin" ]; then
        echo "ERROR: Khong tim thay test_${size}MB.bin"
        exit 1
    fi
done

# Don dep FIFO va log cu
rm -f "$SENDER_FIFO" "$RECEIVER_FIFO"
mkfifo "$SENDER_FIFO" "$RECEIVER_FIFO"

: > "$SENDER_LOG"
: > "$RECEIVER_LOG"

# Mo FIFO o che do doc/ghi de tranh bi block khi mo
exec 3<> "$RECEIVER_FIFO"
exec 4<> "$SENDER_FIFO"

# Khoi dong client nhan va client gui
stdbuf -oL ./client An \
    < "$RECEIVER_FIFO" > "$RECEIVER_LOG" 2>&1 &
RX_PID=$!

sleep 1

stdbuf -oL ./client Truong \
    < "$SENDER_FIFO" > "$SENDER_LOG" 2>&1 &
TX_PID=$!

cleanup() {
    kill "$TX_PID" "$RX_PID" 2>/dev/null || true
    wait "$TX_PID" "$RX_PID" 2>/dev/null || true
    exec 3>&-
    exec 4>&-
    rm -f "$SENDER_FIFO" "$RECEIVER_FIFO"
}

trap cleanup EXIT INT TERM

echo "Dang cho hai client ket noi..."

# Cho client ket noi
CONNECTED=0
for i in $(seq 1 100); do
    if grep -q "Đã kết nối thành công" "$SENDER_LOG" &&
       grep -q "Đã kết nối thành công" "$RECEIVER_LOG"; then
        CONNECTED=1
        break
    fi
    sleep 0.1
done

if [ "$CONNECTED" -ne 1 ]; then
    echo "ERROR: Client khong ket noi duoc."
    echo "Kiem tra server va cac file log:"
    echo "$SENDER_LOG"
    echo "$RECEIVER_LOG"
    exit 1
fi

echo "Hai client da ket noi."
echo

echo "size_MiB,elapsed_seconds,throughput_MiB_per_sec,integrity" > "$RESULTS"

for size in "${SIZES[@]}"; do
    FILE="test_${size}MB.bin"
    RECEIVED="nhan_${FILE}"

    # Dem so lan hoan tat truoc khi gui
    SEND_BEFORE=$(grep -c "Đã gửi file thành công" "$SENDER_LOG" || true)
    RECEIVE_BEFORE=$(grep -c "File đã lưu thành" "$RECEIVER_LOG" || true)

    echo "Dang truyen file ${size} MiB..."

    START=$(date +%s.%N)

    printf '/sendfile An %s\n' "$FILE" >&4

    # Cho ca ben gui va ben nhan bao hoan tat
    DONE=0
    for i in $(seq 1 3600); do
        SEND_NOW=$(grep -c "Đã gửi file thành công" "$SENDER_LOG" || true)
        RECEIVE_NOW=$(grep -c "File đã lưu thành" "$RECEIVER_LOG" || true)

        if [ "$SEND_NOW" -gt "$SEND_BEFORE" ] &&
           [ "$RECEIVE_NOW" -gt "$RECEIVE_BEFORE" ]; then
            DONE=1
            break
        fi

        if ! kill -0 "$TX_PID" 2>/dev/null ||
           ! kill -0 "$RX_PID" 2>/dev/null; then
            echo "ERROR: Client da dung."
            exit 1
        fi

        sleep 0.1
    done

    END=$(date +%s.%N)

    if [ "$DONE" -ne 1 ]; then
        echo "ERROR: Het thoi gian cho file ${size} MiB."
        echo "Kiem tra $SENDER_LOG va $RECEIVER_LOG"
        exit 1
    fi

    # Tinh thoi gian va throughput
    ELAPSED=$(awk -v s="$START" -v e="$END" 'BEGIN {printf "%.3f", e-s}')
    SPEED=$(awk -v size="$size" -v t="$ELAPSED" \
        'BEGIN {if (t>0) printf "%.3f", size/t; else print "0"}')

    # So sanh tung byte file goc va file nhan
    if cmp -s "$FILE" "$RECEIVED"; then
        INTEGRITY="PASS"
    else
        INTEGRITY="FAIL"
    fi

    echo "${size},${ELAPSED},${SPEED},${INTEGRITY}" >> "$RESULTS"

    echo "Kich thuoc: ${size} MiB"
    echo "Thoi gian: ${ELAPSED} giay"
    echo "Throughput: ${SPEED} MiB/s"
    echo "Kiem tra file: ${INTEGRITY}"
    echo
done

echo "Benchmark hoan tat."
echo "Ket qua duoc luu tai: $RESULTS"

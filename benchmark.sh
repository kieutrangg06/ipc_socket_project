#!/bin/bash
set -u

cd "$(dirname "$0")" || exit 1

SIZES=(10 50 100)
SENDER_LOG="benchmark_sender.log"
RECEIVER_LOG="benchmark_receiver.log"
RESULTS="benchmark_results.csv"

SENDER_FIFO="benchmark_sender.in"
RECEIVER_FIFO="benchmark_receiver.in"

if [ ! -S /tmp/ipc_chat_system.sock ]; then
    echo "ERROR: Server chua chay!"
    echo "Hay khoi dong ./server truoc khi chay benchmark."
    exit 1
fi

for size in "${SIZES[@]}"; do
    if [ ! -f "test_${size}MB.bin" ]; then
        echo "Tao file mau test_${size}MB.bin..."
        head -c "${size}M" /dev/urandom > "test_${size}MB.bin"
    fi
done

rm -f "$SENDER_FIFO" "$RECEIVER_FIFO"
mkfifo "$SENDER_FIFO" "$RECEIVER_FIFO"

: > "$SENDER_LOG"
: > "$RECEIVER_LOG"

exec 3<> "$RECEIVER_FIFO"
exec 4<> "$SENDER_FIFO"

stdbuf -oL ./client An < "$RECEIVER_FIFO" > "$RECEIVER_LOG" 2>&1 &
RX_PID=$!

sleep 0.5

stdbuf -oL ./client Truong < "$SENDER_FIFO" > "$SENDER_LOG" 2>&1 &
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
    exit 1
fi

echo "Hai client da ket noi thanh cong."
echo "size_MiB,elapsed_seconds,throughput_MiB_per_sec,integrity" > "$RESULTS"

for size in "${SIZES[@]}"; do
    FILE="test_${size}MB.bin"
    RECEIVED="nhan_${FILE}"

    SEND_BEFORE=$(grep -c "Đã gửi file thành công" "$SENDER_LOG" || true)
    RECEIVE_BEFORE=$(grep -c "File đã lưu thành" "$RECEIVER_LOG" || true)

    echo "Dang truyen file ${size} MiB..."
    START=$(date +%s.%N)

    printf '/sendfile An %s\n' "$FILE" >&4

    DONE=0
    for i in $(seq 1 3600); do
        SEND_NOW=$(grep -c "Đã gửi file thành công" "$SENDER_LOG" || true)
        RECEIVE_NOW=$(grep -c "File đã lưu thành" "$RECEIVER_LOG" || true)

        if [ "$SEND_NOW" -gt "$SEND_BEFORE" ] && [ "$RECEIVE_NOW" -gt "$RECEIVE_BEFORE" ]; then
            DONE=1
            break
        fi

        if ! kill -0 "$TX_PID" 2>/dev/null || ! kill -0 "$RX_PID" 2>/dev/null; then
            echo "ERROR: Client da dung dot ngot."
            exit 1
        fi
        sleep 0.1
    done

    END=$(date +%s.%N)

    if [ "$DONE" -ne 1 ]; then
        echo "ERROR: Timeout file ${size} MiB."
        exit 1
    fi

    ELAPSED=$(awk -v s="$START" -v e="$END" 'BEGIN {printf "%.3f", e-s}')
    SPEED=$(awk -v size="$size" -v t="$ELAPSED" 'BEGIN {if (t>0) printf "%.3f", size/t; else print "0"}')

    if cmp -s "$FILE" "$RECEIVED"; then
        INTEGRITY="PASS"
    else
        INTEGRITY="FAIL"
    fi

    echo "${size},${ELAPSED},${SPEED},${INTEGRITY}" >> "$RESULTS"
    echo " -> Hoan tat: ${size} MiB trong ${ELAPSED}s | Toc do: ${SPEED} MiB/s | Kiem tra: ${INTEGRITY}"
    echo
done

echo "Benchmark hoan tat! Ket qua luu tai: $RESULTS"

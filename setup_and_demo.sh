#!/bin/bash
set -e

echo "[1/4] Cleaning up previous environment..."
make clean
pkill -9 -f ./server 2>/dev/null || true
rm -f /tmp/ipc_chat_system.sock

echo "[2/4] Compiling project under strict POSIX flags (-Wall -Wextra -O2)..."
make

echo "[3/4] Generating 10MB synthetic binary payload for Demo..."
head -c 10M /dev/urandom > test_demo.bin

echo "[4/4] Granting script execution permissions..."
chmod +x benchmark.sh setup_and_demo.sh

echo "=========================================================="
echo "==> SYSTEM READY FOR EXECUTION AND LIVE DEMO!"
echo "=========================================================="

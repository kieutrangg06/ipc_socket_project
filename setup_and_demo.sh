#!/bin/bash
set -e

echo "[1/4] Dọn dẹp môi trường cũ..."
make clean
pkill -9 -f ./server 2>/dev/null || true
rm -f /tmp/ipc_chat_system.sock

echo "[2/4] Biên dịch dự án chuẩn POSIX (-Wall -Wextra -O2)..."
make

echo "[3/4] Tạo file dữ liệu nhị phân mẫu 10MB phục vụ Demo..."
head -c 10M /dev/urandom > test_demo.bin

echo "[4/4] Cấp quyền thực thi script..."
chmod +x benchmark.sh setup_and_demo.sh

echo "=========================================================="
echo "==> DỰ ÁN ĐÃ SẴN SÀNG ĐỂ VẬN HÀNH VÀ DEMO!"
echo "=========================================================="

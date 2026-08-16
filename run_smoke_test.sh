#!/usr/bin/env bash
# run_smoke_test.sh
# 在容器内自测：运行 flight_logger_smoke N 秒后发 SIGINT，检查 mcap 输出
set -euo pipefail

PROJECT=/home/ros/00-RosSpace/24-mcapper
BINARY=$PROJECT/build/flight_logger_smoke
LOG_DIR=$PROJECT/logs

mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*.mcap

echo "[test] 启动 flight_logger_smoke ..."
"$BINARY" &
SMOKE_PID=$!
echo "[test] PID = $SMOKE_PID"

# 等待 5 秒推数据
sleep 5

echo "[test] 发送 SIGINT -> PID $SMOKE_PID"
kill -INT "$SMOKE_PID"

# 等待进程退出（stop() 内部等 post_trigger 写完后才返回）
wait "$SMOKE_PID" || true

echo ""
echo "[test] ✅ 程序已退出，检查 logs/ 目录："
ls -lh "$LOG_DIR"
echo ""
echo "[test] 文件字节数："
du -b "$LOG_DIR"/*.mcap 2>/dev/null || echo "  (无 .mcap 文件)"

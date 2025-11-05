#!/bin/bash

# --- การตั้งค่า Test Case ---
THREAD_COUNTS="1 2 4 8"
NUM_CLIENTS=100          # จำนวน Client ที่จะรันพร้อมกัน
MESSAGES_PER_CLIENT=50   # จำนวนข้อความที่ Client แต่ละตัวจะส่ง

# ---------------------------------
# 0. สร้าง Directory สำหรับ Log และ Result
# ---------------------------------
mkdir -p log result
echo "Created 'log' and 'result' directories."
echo ""

# ---------------------------------
# 1. คอมไพล์โปรแกรม
# ---------------------------------
echo "Compiling server and load_tester..."

# คอมไพล์ Server
if ! g++ -o ../exe/server ../server/server.cpp -lrt -pthread -std=c++17; then
    echo "Failed to compile server_new.cpp. Aborting."
    exit 1
fi

# คอมไพล์ Load Tester
if ! g++ -o ../exe/load_tester load_tester.cpp -lrt -pthread -std=c++17; then
    echo "Failed to compile load_tester.cpp. Aborting."
    exit 1
fi

echo "Compilation successful."
echo "---------------------------------"

# ---------------------------------
# 2. เริ่มการทดสอบ
# ---------------------------------
TOTAL_MESSAGES=$(($NUM_CLIENTS * $MESSAGES_PER_CLIENT))
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="result/throughput_${TIMESTAMP}.txt"

echo "Starting throughput test..."
echo "Total Clients: $NUM_CLIENTS"
echo "Messages/Client: $MESSAGES_PER_CLIENT"
echo "Total Messages: $TOTAL_MESSAGES"
echo "Results will be saved to: $RESULT_FILE"
echo "---------------------------------"
echo ""

# 🔧 เขียน header ไปยัง result file
{
    echo "====== Throughput Test Results ======"
    echo "Timestamp: $TIMESTAMP"
    echo "Total Clients: $NUM_CLIENTS"
    echo "Messages/Client: $MESSAGES_PER_CLIENT"
    echo "Total Messages: $TOTAL_MESSAGES"
    echo "======================================"
    echo ""
} > "$RESULT_FILE"

# ---------------------------------
# 3. ทดสอบแต่ละจำนวน Thread
# ---------------------------------
for N_THREADS in $THREAD_COUNTS; do
    echo "--- Testing with $N_THREADS server threads ---"

    # เริ่ม Server (ใน Background)
    echo "[Server] Starting server with $N_THREADS threads..."
    SERVER_LOG="log/server_${N_THREADS}threads_${TIMESTAMP}.log"
    stdbuf -oL ../exe/server $N_THREADS > "$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    # รอให้ Server พร้อม
    sleep 2

    if ! ps -p $SERVER_PID > /dev/null; then
        echo "Server (PID: $SERVER_PID) failed to start. Check $SERVER_LOG"
        exit 1
    fi
    echo "[Server] Server started (PID: $SERVER_PID)."

    # เริ่มจับเวลา
    start_time=$(date +%s.%N)

    # รัน Client ทั้งหมดพร้อมกัน
    echo "[Clients] Spawning $NUM_CLIENTS clients..."
    CLIENT_PIDS=""
    for i in $(seq 1 $NUM_CLIENTS); do
        ../exe/load_tester "client_$i" $MESSAGES_PER_CLIENT > "log/client_${i}_${N_THREADS}threads_${TIMESTAMP}.log" 2>&1 &
        CLIENT_PIDS="$CLIENT_PIDS $!"
    done

    # รอ Client ทั้งหมดทำงานเสร็จ
    echo "[Clients] Waiting for all clients to finish..."
    for pid in $CLIENT_PIDS; do
        wait $pid
    done
    echo "[Clients] All clients finished."

    # หยุดจับเวลา
    end_time=$(date +%s.%N)

    # หยุด Server
    echo "[Server] Stopping server (PID: $SERVER_PID)..."
    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    echo "[Server] Server stopped."

    # คำนวณผลลัพธ์
    total_time=$(echo "$end_time - $start_time" | bc -l)
    throughput=$(echo "scale=2; $TOTAL_MESSAGES / $total_time" | bc -l)

    echo ""
    echo "--- Results ($N_THREADS Threads) ---"
    echo "Total Time Taken: $total_time seconds"
    echo "Total Messages Sent: $TOTAL_MESSAGES"
    echo "Throughput: $throughput messages/second"
    echo "---------------------------------"
    echo ""

    # เขียนผลลัพธ์ไปยัง result file
    {
        echo "--- Test with $N_THREADS Threads ---"
        echo "Total Time Taken: $total_time seconds"
        echo "Total Messages Sent: $TOTAL_MESSAGES"
        echo "Throughput: $throughput messages/second"
        echo "Server Log: $SERVER_LOG"
        echo ""
    } >> "$RESULT_FILE"

    # รอ 1 วินาที ก่อนเริ่มรอบถัดไป
    sleep 1
done

# ---------------------------------
# 4. สรุปผลการทดสอบ
# ---------------------------------
echo "Test finished."
echo "Results saved to: $RESULT_FILE"
echo "Logs saved to: log/"
echo ""
echo "Cleaning up compiled files..."
rm -f server_new load_tester
echo "Done."

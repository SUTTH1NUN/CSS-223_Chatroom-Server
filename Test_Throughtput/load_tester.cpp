// บันทึกเป็น: load_tester.cpp
// (โค้ดนี้เหมือนกับในคำตอบก่อนหน้า)

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// --- POSIX C Libraries ---
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// --- Queue Settings ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;

std::string g_clientQueueName;

// --- ฟังก์ชันส่งคำสั่ง (จาก client.cpp) ---
int sendCommand(const std::string& cmd, const std::string& payload) {
    mqd_t server_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (server_mq == (mqd_t)-1) {
        // ถ้า Server ยังไม่พร้อม ให้ลองใหม่
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
        if (server_mq == (mqd_t)-1) {
            std::cerr << "[" << g_clientQueueName << "] Error: Cannot open server queue.\n";
            return errno;
        }
    }

    std::string message = cmd + "|" + g_clientQueueName + payload;
    if (mq_send(server_mq, message.c_str(), message.size() + 1, 0) == -1) {
        int err = errno;
        mq_close(server_mq);
        std::cerr << "[" << g_clientQueueName << "] Error: mq_send failed: " << strerror(err) << "\n";
        return err;
    }

    mq_close(server_mq);
    return 0; // 0 หมายถึงสำเร็จ
}

// --- Main (แบบไม่โต้ตอบ) ---
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./load_tester <UsernamePrefix> <NumMessages>\n";
        return 1;
    }

    std::string myName = std::string(argv[1]) + "_" + std::to_string(getpid());
    int numMessages = std::stoi(argv[2]);
    std::string myRoom = "room_" + myName;
    g_clientQueueName = "/reply_" + myName;

    // 1. สร้างคิวส่วนตัว
    // Tester ไม่จำเป็นต้อง "อ่าน" คิว แต่ "ต้องสร้าง"
    // เพราะ Server จะพยายาม "ส่ง" ตอบกลับมา
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    
    mq_unlink(g_clientQueueName.c_str()); // ลบของเก่า
    mqd_t my_mq = mq_open(g_clientQueueName.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    if (my_mq == (mqd_t)-1) {
        perror("Tester: mq_open (create)");
        return 1;
    }
    mq_close(my_mq);

    // 2. ลงทะเบียน
    if (sendCommand("REGISTER", "|" + myName) != 0) return 1;

    // 3. สร้างห้อง
    if (sendCommand("CREATE", "|" + myRoom + "|" + myName) != 0) return 1;

    // 4. ยิงข้อความ
    for (int i = 0; i < numMessages; ++i) {
        std::string msg = "This is message " + std::to_string(i+1);
        if (sendCommand("CHAT", "|" + myRoom + "|" + myName + "|" + msg) != 0) {
            // ถ้าคิว Server เต็ม อาจจะส่งไม่สำเร็จ ให้รอแป๊บนึง
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // 5. ออกจากระบบ
    sendCommand("EXIT", "|" + myName);
    
    // 6. ลบคิวตัวเอง
    // (เรา sleep 50 ms เพื่อให้ Server มีเวลาประมวลผล EXIT และเลิกยุ่งกับคิวเรา)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mq_unlink(g_clientQueueName.c_str());

    return 0;
}
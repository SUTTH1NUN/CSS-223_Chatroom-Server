// ./chat_client
// g++ -o chat_client client.cpp -lrt -pthread -std=c++17

// --- C++ Standard Libraries ---
#include <iostream>     // สำหรับ std::cout, std::cerr, std::cin, std::endl
#include <string>       // สำหรับ std::string, std::getline, std::to_string
#include <sstream>      // สำหรับ std::stringstream
#include <thread>       // สำหรับ std::thread
#include <mutex>        // สำหรับ std::mutex, std::lock_guard
#include <atomic>       // สำหรับ std::atomic
#include <chrono>       // สำหรับ std::chrono::seconds, std::chrono::milliseconds

// --- POSIX C Libraries ---
#include <mqueue.h>     // สำหรับ mq_open, mq_receive, mq_send, ...
#include <fcntl.h>      // สำหรับ O_RDONLY, O_WRONLY, O_CREAT, ...
#include <sys/stat.h>   // สำหรับ S_IRUSR, S_IWUSR (mode flags)
#include <unistd.h>     // สำหรับ getpid()
#include <errno.h>      // สำหรับ errno
#include <signal.h>     // สำหรับ signal, SIGINT, SIGTERM
#include <time.h>       // สำหรับ clock_gettime, timespec
#include <string.h>     // สำหรับ strerror()

// --- Queue Settings ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;

// --- Global State ---
// ใช้ atomic เพื่อให้แน่ใจว่าการอ่าน/เขียนค่าจากหลาย Thread ปลอดภัย
std::atomic<bool> g_running(true);        // ธงส่วนกลางสำหรับสั่งให้ทุก Thread หยุดทำงาน
std::atomic<bool> g_registered(false);  // ธงว่า Server ยืนยันการลงทะเบียนหรือยัง

std::string g_myName;
std::string g_clientQueueName;
std::string g_currentRoom = "";

std::mutex g_room_mutex;   // Mutex สำหรับป้องกันการเข้าถึง g_currentRoom พร้อมกัน
std::mutex g_cout_mutex;   // Mutex สำหรับป้องกัน std::cout ตีกันระหว่าง Thread

// --- Prototypes ---
void receiverThread();
int sendCommand(const std::string& cmd, const std::string& payload);
void showPrompt();
void handle_sigint(int);

// ------------------------
// Signal Handler (จัดการ Ctrl+C)
// ------------------------
void handle_sigint(int) {
    std::cout << "\n[Client] Caught SIGINT, disconnecting..." << std::endl;
    if (g_registered) {
        // ส่งคำสั่ง EXIT บอก Server ก่อน (ถ้าทำได้)
        sendCommand("EXIT", "|" + g_myName);
    }
    // สั่งให้ Thread อื่นๆ หยุดทำงาน
    g_running = false;
}

// ------------------------
// Thread รับข้อความจาก Server (สำคัญมาก)
// ------------------------
void receiverThread() {
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    
    mqd_t my_mq = mq_open(g_clientQueueName.c_str(), O_RDONLY);
    if (my_mq == (mqd_t)-1) {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cerr << "[CLIENT ERROR] Failed to open client queue " << g_clientQueueName << std::endl;
        g_running = false;
        return;
    }
    
    char buf[MQ_MSGSIZE];
    struct timespec ts;

    while (g_running) {
        //! --- นี่คือส่วนที่สำคัญที่สุดในการป้องกัน Client ค้าง ---
        // 1. ตั้ง Timeout 1 วินาที
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // รอรับข้อความได้สูงสุด 1 วินาที

        // 2. ใช้ mq_timedreceive (แทน mq_receive)
        //    นี่คือ Non-Blocking call แบบมี timeout
        ssize_t bytes = mq_timedreceive(my_mq, buf, MQ_MSGSIZE, nullptr, &ts);
        
        if (bytes > 0) {
            // == ได้รับข้อความ ==
            std::string response(buf);
            
            size_t sep = response.find('|');
            std::string type = (sep == std::string::npos) ? "" : response.substr(0, sep);
            std::string message = (sep == std::string::npos) ? response : response.substr(sep + 1);

            // ล็อค cout เพื่อป้องกันการพิมพ์ชนกับ main thread
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cout << "\n";
            
            if (type == "SYSTEM") {
                std::cout << "[SYSTEM] " << message;
                
                if (message.find("Welcome") != std::string::npos) {
                    g_registered = true;
                }
                // ถ้า Server สั่งปิด (เช่น โดนเตะ หรือ Server ปิด)
                else if (message.find("disconnected") != std::string::npos || 
                         message.find("Goodbye") != std::string::npos) {
                    g_running = false;
                }
            } 
            else if (type == "LIST") {
                std::cout << "[ROOMS] " << message;
            } 
            else if (type == "CHAT") {
                std::string current;
                {
                    std::lock_guard<std::mutex> room_lock(g_room_mutex);
                    current = g_currentRoom;
                }
                std::cout << "[" << current << "] " << message;
            } 
            else if (type == "DM") {
                std::cout << "[DM] " << message;
            } 
            else if (type == "JOIN_SUCCESS") {
                {
                    std::lock_guard<std::mutex> room_lock(g_room_mutex);
                    g_currentRoom = message;
                }
                
                if (message.empty()) {
                    std::cout << "[SYSTEM] Returned to Lobby.";
                } else {
                    std::cout << "[SYSTEM] Successfully joined room '" << message << "'.";
                }
            } 
            else {
                std::cout << "[RAW] " << response;
            }
            
            std::cout << std::endl;
            showPrompt(); // แสดง prompt ใหม่หลังรับข้อความ
        }
        
        else if (bytes == -1) {
            // == ไม่ได้รับข้อความ หรือ Error ==
            if (errno == ETIMEDOUT) {
                //! สำคัญ: ถ้า Timeout (ครบ 1 วินาที)
                // นี่เป็นเรื่องปกติ ไม่ใช่ Error
                // เราแค่ต้องวน Loop กลับไปเช็ค g_running ใหม่
                continue;
            } 
            else if (g_running) {
                // ถ้าเป็น Error อื่น (เช่น คิวพัง)
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                perror("[CLIENT ERROR] mq_timedreceive");
                g_running = false; // สั่งปิด
            }
        }
    }
    
    mq_close(my_mq);
}

// ------------------------
// ฟังก์ชันส่งคำสั่งไปยัง Server
// ------------------------
//! สำคัญ: คืนค่า 0 ถ้าสำเร็จ, คืนค่า 'errno' ถ้าล้มเหลว
// เราใช้ค่า errno นี้เพื่อตรวจจับว่า Server ล่มหรือไม่
int sendCommand(const std::string& cmd, const std::string& payload) {
    // O_NONBLOCK: ถ้าคิวของ Server เต็ม, mq_send จะไม่ค้าง (fail ทันที)
    mqd_t server_mq = mq_open(CONTROL_QUEUE, O_WRONLY | O_NONBLOCK);
    if (server_mq == (mqd_t)-1) {
        // ไม่ต้อง print error ที่นี่ ให้ Thread ที่เรียกไปจัดการเอง
        return errno; // คืนค่า error code
    }

    std::string message = cmd + "|" + g_clientQueueName + payload;
    if (mq_send(server_mq, message.c_str(), message.size() + 1, 0) == -1) {
        int err = errno;
        mq_close(server_mq);
        return err; // คืนค่า error code
    }

    mq_close(server_mq);
    return 0; // 0 หมายถึงสำเร็จ
}

// ------------------------
// แสดง Prompt
// ------------------------
void showPrompt() {
    // ล็อค g_room_mutex เพื่ออ่าน g_currentRoom อย่างปลอดภัย
    std::lock_guard<std::mutex> lock(g_room_mutex);
    if (g_currentRoom.empty()) {
        std::cout << "[Lobby] > ";
    } else {
        std::cout << "[" << g_currentRoom << "] > ";
    }
    std::cout.flush(); // บังคับให้แสดงผลทันที
}

// ------------------------
// MAIN
// ------------------------
int main(int argc, char* argv[]) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    std::cout << "Enter your name: ";
    std::getline(std::cin, g_myName);
    
    if (g_myName.empty()) {
        std::cerr << "[ERROR] Name cannot be empty.\n";
        return 1;
    }
    
    // สร้างชื่อคิวส่วนตัวที่ไม่ซ้ำกัน
    g_clientQueueName = "/reply_" + g_myName + "_" + std::to_string(getpid());
    
    //* สร้างคิวส่วนตัวของ Client ก่อน
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    
    mq_unlink(g_clientQueueName.c_str()); // ลบคิวเก่าที่อาจค้าง
    mqd_t test_mq = mq_open(g_clientQueueName.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    if (test_mq == (mqd_t)-1) {
        perror("[ERROR] Cannot create client queue");
        return 1;
    }
    mq_close(test_mq);
    
    //* เริ่ม receiver thread ก่อน
    // (เพื่อให้พร้อมรับข้อความ "Welcome" ทันทีที่ลงทะเบียน)
    std::thread receiver(receiverThread);
    std::cout << "[Client] Receiver thread started.\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    //* ส่ง REGISTER
    std::cout << "[Client] Sending registration...\n";
    int err = sendCommand("REGISTER", "|" + g_myName);
    if (err != 0) {
        // ถ้าส่งไม่สำเร็จ (เช่น Server ยังไม่เปิด)
        std::cerr << "[ERROR] Could not send registration. Server might be down (" << strerror(err) << ").\n";
        g_running = false; // สั่งปิด receiver thread
        if (receiver.joinable()) receiver.join();
        mq_unlink(g_clientQueueName.c_str());
        return 1;
    }
    
    //* รอ Server ยืนยัน (สูงสุด 5 วินาที)
    std::cout << "[Client] Waiting for registration confirmation...\n";
    int timeout = 0;
    while (!g_registered && g_running && timeout < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timeout++;
    }
    
    if (!g_registered) {
        std::cerr << "[ERROR] Registration timeout or failed.\n";
        g_running = false;
        if (receiver.joinable()) receiver.join();
        mq_unlink(g_clientQueueName.c_str());
        return 1;
    }

    std::cout << "[Client] Registration successful!\n";
    
    //* เริ่ม Heartbeat Thread (หลังจาก Register สำเร็จ)
    std::thread heartbeat([](){
        while (g_running && g_registered) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_running) break;
            
            int err = sendCommand("PING", "|" + g_myName);
            
            // --- ‼️ นี่คือส่วนที่สำคัญที่สุดในการตรวจจับ Server ล่ม ‼️ ---
            if (err == ENOENT) {
                // ENOENT (No such file or directory)
                // หมายความว่า CONTROL_QUEUE ของ Server หายไป (Server ล่ม)
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cerr << "\n[CLIENT] Server connection lost. Shutting down..." << std::endl;
                g_running = false; // ‼️ สั่งปิด Client
            } 
            else if (err != 0) {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cerr << "\n[CLIENT] Heartbeat send error: " << strerror(err) << std::endl;
            }
        }
    });

    std::cout << "\n--- Commands ---\n";
    std::cout << " /list          - Show all Rooms and member counts\n";
    std::cout << " /create <room> - Create and join room\n";
    std::cout << " /join <room>   - Join Room\n";
    std::cout << " /leave         - Leave current room and return to Lobby\n";
    std::cout << " /who           - Show users in current room\n";
    std::cout << " /dm <name> <msg> - Send Direct Message\n";
    std::cout << " /members       - Show all online users\n";
    std::cout << " /exit          - Disconnect and Quit\n";
    std::cout << " (Type message to chat in room)\n";
    std::cout << "----------------\n\n";

    // --- Main Input Loop ---
    std::string input;
    while (g_running) {
        showPrompt();
        
        // รอรับคำสั่งจาก User
        if (!std::getline(std::cin, input)) {
            // ถ้า Ctrl+D (EOF)
            g_running = false;
            break;
        }
        
        if (!g_running) break; // ตรวจสอบอีกครั้ง เผื่อ heartbeat สั่งปิด
        if (input.empty()) continue;

        if (input[0] == '/') {
            // --- xử lý Command ---
            std::stringstream ss(input);
            std::string cmd;
            ss >> cmd;
            
            if (cmd == "/exit") {
                sendCommand("EXIT", "|" + g_myName);
                g_running = false;
                break;
            }
            else if (cmd == "/list") {
                int err = sendCommand("LIST", "|" + g_myName);
                if (err == ENOENT) g_running = false; // ตรวจสอบ Server ล่ม
            }
            else if (cmd == "/who") {
                std::string current;
                {
                    std::lock_guard<std::mutex> lock(g_room_mutex);
                    current = g_currentRoom;
                }
                if (current.empty()) {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cout << "[ERROR] You must be in a room to use /who.\n";
                } else {
                    int err = sendCommand("WHO", "|" + g_myName);
                    if (err == ENOENT) g_running = false;
                }
            }
            else if (cmd == "/leave") {
                std::string current;
                {
                    std::lock_guard<std::mutex> lock(g_room_mutex);
                    current = g_currentRoom;
                }
                if (current.empty()) {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cout << "[ERROR] You are already in the Lobby.\n";
                } else {
                    int err = sendCommand("LEAVE", "|" + g_myName);
                    if (err == ENOENT) g_running = false;
                }
            }
            else if (cmd == "/create" || cmd == "/join") {
                std::string room;
                ss >> room;
                if (room.empty()) {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cout << "[ERROR] Usage: " << cmd << " <room_name>\n";
                } else {
                    std::string payload_cmd = (cmd == "/create") ? "CREATE" : "JOIN";
                    int err = sendCommand(payload_cmd, "|" + room + "|" + g_myName);
                    if (err == ENOENT) g_running = false;
                }
            }
            else if (cmd == "/dm") {
                std::string target, msg_part;
                ss >> target;
                if (target.empty() || !(ss >> std::ws && std::getline(ss, msg_part))) {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cout << "[ERROR] Usage: /dm <name> <message>\n";
                } else {
                    int err = sendCommand("DM", "|" + target + "|" + g_myName + "|" + msg_part);
                    if (err == ENOENT) g_running = false;
                }
            }
            else if (cmd == "/members") {
                int err = sendCommand("MEMBERS", "|" + g_myName);
                if (err == ENOENT) g_running = false;
            }
            else {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << "[ERROR] Unknown command: " << cmd << std::endl;
            }
        }
        else {
            // --- xử lý Chat Message ---
            std::string current;
            {
                std::lock_guard<std::mutex> lock(g_room_mutex);
                current = g_currentRoom;
            }
            
            if (current.empty()) {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << "[ERROR] You must be in a room to chat. Use /create or /join.\n";
            } else {
                int err = sendCommand("CHAT", "|" + current + "|" + g_myName + "|" + input);
                if (err == ENOENT) g_running = false; // ตรวจสอบ Server ล่ม
            }
        }
    }
    
    // --- Shutdown ---
    g_running = false; // เผื่อว่า Loop จบด้วยเหตุผลอื่น
    
    // รอให้ Thread อื่นๆ ปิดตัวลงอย่างสมบูรณ์
    if (receiver.joinable()) receiver.join();
    if (heartbeat.joinable()) heartbeat.join();
    
    // ลบไฟล์คิวของตัวเอง
    mq_unlink(g_clientQueueName.c_str());

    std::cout << "\n[CLIENT] Disconnected" << std::endl;
    return 0;
}
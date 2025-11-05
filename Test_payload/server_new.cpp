// docker build -t chat-server-mq .
// docker run --name chat-server --privileged -it chat-server-mq ./server 4

// --- C++ Standard Libraries ---
#include <iostream>     // สำหรับ std::cout, std::cerr, std::cin, std::endl
#include <string>     // สำหรับ std::string, std::getline, std::to_string
#include <sstream> // สำหรับ std::stringstream
#include <vector>     // สำหรับ std::vector
#include <map>      // สำหรับ std::map
#include <queue>      // สำหรับ std::queue (Thread Pool)
#include <thread>     // สำหรับ std::thread
#include <mutex>      // สำหรับ std::mutex, std::lock_guard, std::unique_lock
#include <condition_variable> // สำหรับ std::condition_variable (Thread Pool)
#include <atomic>     // สำหรับ std::atomic_bool (g_server_running)
#include <chrono>     // สำหรับ std::chrono::seconds, std::chrono::milliseconds

// --- POSIX C Libraries ---
#include <mqueue.h>// สำหรับ mq_open, mq_receive, mq_send, ...
#include <fcntl.h>// สำหรับ O_RDONLY, O_WRONLY, O_CREAT, ...
#include <sys/stat.h>// สำหรับ S_IRUSR, S_IWUSR (mode flags)
#include <unistd.h>// สำหรับ getpid()
#include <errno.h>// สำหรับ errno
#include <signal.h>// สำหรับ signal, SIGINT, SIGTERM
#include <time.h>     // สำหรับ time, strftime
#include <string.h>// สำหรับ strerror()

// ใช้ std:: prefix เพื่อความชัดเจน
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::map;
using std::vector;
using std::queue;
using std::stringstream;
using std::to_string;
using std::mutex;
using std::lock_guard;
using std::unique_lock;
using std::thread;

// --- Queue Settings ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;
mqd_t mq;

// --- โครงสร้างข้อมูลสำหรับติดตามสถานะ ---
struct ClientInfo {
    string username;
    string reply_queue;
    string current_room;
};

struct Room {
    string name;
};

// --- Global State & Mutexes ---
map<string, ClientInfo> clients;
mutex clients_mutex;    // ‼️ Mutex ระดับ 1 (ต้องล็อคก่อน)
map<string, Room> rooms;
mutex rooms_mutex;      // ‼️ Mutex ระดับ 2 (ต้องล็อคทีหลัง)

map<string, time_t> room_last_active;
mutex room_mutex;
map<string, time_t> last_heartbeat;
mutex hb_mutex;
map<string, time_t> last_active;
mutex active_mutex;

// --- Worker Thread Pool ---
queue<string> task_queue;        // คิวงาน (ข้อความที่ได้รับ)
mutex queue_mutex;           // Mutex สำหรับป้องกัน task_queue
std::condition_variable queue_cond;    // ตัวส่งสัญญาณให้ Worker ตื่น
std::atomic<bool> g_server_running(true); // Flag สากลสำหรับสั่งหยุด

// --- Helper Function: ส่งข้อความตอบกลับ ---
void send_reply(const string& reply_q, const string& text) {
    if (reply_q.empty()) return;
    // O_NONBLOCK: ถ้าคิว client เต็ม (อาจจะค้าง) ให้ fail ทันที
    mqd_t client_q = mq_open(reply_q.c_str(), O_WRONLY | O_NONBLOCK);
    if (client_q != (mqd_t)-1) {
        mq_send(client_q, text.c_str(), text.size() + 1, 0);
        mq_close(client_q);
    }
}

// --- Helper Function: ดึงเวลาปัจจุบัน ---
string currentTime() {
    time_t now = time(nullptr);
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
    return string(buf);
}

// --- ‼️ FIX 1: แก้ไข broadcast_to_room (ป้องกัน Deadlock) ---
void broadcast_to_room(const string& room_name, const string& sender_name, const string& message) {
    if (room_name.empty()) return; // ถ้า room ว่าง (เช่น lobby) ไม่ต้องทำ

    vector<string> recipient_queues;
    {
        // --- ปฏิบัติตามกฎ: ล็อค 1 (clients) ก่อน 2 (rooms) ---
        lock_guard<mutex> lock1(clients_mutex);
        lock_guard<mutex> lock2(rooms_mutex);

        // ถ้าห้องถูกลบไปแล้ว ก็ไม่ต้องทำ
        if (rooms.count(room_name) == 0) return;

        // 1. รวบรวม "คิว" ที่จะส่ง (ทำงานเร็วๆ)
        for (auto const& [name, info] : clients) {
            if (info.current_room == room_name && name != sender_name) {
                recipient_queues.push_back(info.reply_queue);
            }
        }
    } // ‼️ ปลดล็อค rooms_mutex และ clients_mutex ทันที

    string timeStr = "[" + currentTime() + "] ";
    string full_message = "CHAT|" + timeStr + sender_name + ": " + message;

    // 2. ส่งข้อความ (ทำงานช้าๆ) "นอก" Lock
    for (const auto& q : recipient_queues) {
        send_reply(q, full_message);
    }
}
// --- ‼️ END FIX 1 ---

// --- Helper Function: นับสมาชิกในห้อง (Thread-safe) ---
int count_members_in_room(const string& room_name) {
    int count = 0;
    lock_guard<mutex> lock(clients_mutex); // ล็อค clients (ระดับ 1)
    for (auto const& [name, info] : clients) {
        if (info.current_room == room_name) {
            count++;
        }
    }
    return count;
} // (ฟังก์ชันนี้ OK)

// --- Signal Handler (สำหรับ Thread Pool) ---
void handle_sigint(int) {
    cout << "\n[Server] Caught SIGINT, shutting down..." << endl;

    g_server_running = false;
    queue_cond.notify_all();

    mqd_t self_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (self_mq != (mqd_t)-1) {
        const char* stop_msg = "STOP|";
        mq_send(self_mq, stop_msg, strlen(stop_msg) + 1, 0);
        mq_close(self_mq);
    }
}

// --- อัปเดตเวลากิจกรรมล่าสุด (Thread-safe) ---
void update_activity(const string& username) {
    lock_guard<mutex> lock(active_mutex);
    last_active[username] = time(nullptr);
}

//! --- ฟังก์ชันประมวลผลข้อความ (หัวใจหลัก) ---
void process_message(const string& msg) {
    vector<string> parts;
    stringstream ss(msg);
    string token;
    while (getline(ss, token, '|')) parts.push_back(token);
    if (parts.empty() || parts.size() < 2) return;

    string cmd = parts[0];
    string reply_q = parts[1];
    string username = (parts.size() >= 3) ? parts[2] : "";

    // --- 1. REGISTER ---
    if (cmd == "REGISTER" && parts.size() >= 3) {
        username = parts[2];
        update_activity(username);

        lock_guard<mutex> lock(clients_mutex); //! ล็อค (1)
        if (clients.count(username)) {
            send_reply(reply_q, "SYSTEM|Error: Username already taken.");
            return;
        }
        clients[username] = {username, reply_q, ""};
        send_reply(reply_q, "SYSTEM|Welcome " + username + "! You are in the Lobby.");
        cout << "[LOG] USER_REG: " << username << " registered (Q: " << reply_q << ")\n";
    }

    // --- 2. CREATE ---
    else if (cmd == "CREATE" && parts.size() >= 4) {
        string room_name = parts[2];
        username = parts[3];
        update_activity(username);

        { //! ล็อค 2 ชั้น (ตามกฎ 1 -> 2)
            lock_guard<mutex> lock1(clients_mutex); // ล็อค 1
            lock_guard<mutex> lock2(rooms_mutex);   // ล็อค 2

            if (clients.count(username) == 0) {
                send_reply(reply_q, "SYSTEM|Error: User not registered.");
                return;
            }
            if (rooms.count(room_name)) {
                send_reply(reply_q, "SYSTEM|Error: Room already exists: " + room_name);
                return;
            }
            if (!clients[username].current_room.empty()) {
                send_reply(reply_q, "SYSTEM|Error: You must be in the Lobby to create a room.");
                return;
            }
            // ถ้าผ่านหมด
            rooms[room_name] = Room{room_name};
            clients[username].current_room = room_name;
        } //! ปลดล็อค

        {
            lock_guard<mutex> lock(room_mutex);
            room_last_active[room_name] = time(nullptr);
        }
        send_reply(reply_q, "JOIN_SUCCESS|" + room_name);
        cout << "[LOG] ROOM_CREATE: " << username << " created and joined room '" << room_name << "'.\n";
    }

    // --- ‼️ FIX 2: แก้ไขคำสั่ง JOIN ---
    // --- 3. JOIN ---
    else if (cmd == "JOIN" && parts.size() >= 4) {
        string room_name = parts[2];
        username = parts[3];
        update_activity(username);

        { //! ล็อค 2 ชั้น (แก้ไขลำดับตามกฎ 1 -> 2)
            lock_guard<mutex> lock1(clients_mutex); // 1. ล็อค clients ก่อน
            lock_guard<mutex> lock2(rooms_mutex);   // 2. ล็อค rooms ทีหลัง

            // ตรวจสอบ User ก่อน (เพราะถือ lock1)
            if (clients.count(username) == 0) {
                send_reply(reply_q, "SYSTEM|Error: User not found.");
                return;
            }
            // ตรวจสอบ Room (เพราะถือ lock2)
            if (rooms.count(room_name) == 0) {
                send_reply(reply_q, "SYSTEM|Error: Room not found.");
                return;
            }
            clients[username].current_room = room_name;
        } //! ปลดล็อค

        {
            lock_guard<mutex> lock(room_mutex);
            room_last_active[room_name] = time(nullptr);
        }
        send_reply(reply_q, "JOIN_SUCCESS|" + room_name);
        broadcast_to_room(room_name, "SYSTEM", username + " has joined.");
        cout << "[LOG] ROOM_JOIN: " << username << " joined room '" << room_name << "'.\n";
    }
    // --- ‼️ END FIX 2 ---

    // --- 4. LIST ---
    else if (cmd == "LIST" && parts.size() >= 3) {
        username = parts[2];
        update_activity(username);
        string result = "LIST|Available Rooms: ";

        // --- (โค้ดส่วนนี้ OK: ล็อค 1, ปลดล็อค 1, ล็อค 2, ปลดล็อค 2) ---
        map<string, int> counts;
        {
            lock_guard<mutex> lock(clients_mutex);
            for (auto const& [name, info] : clients) {
                if (!info.current_room.empty()) {
                    counts[info.current_room]++;
                }
            }
        } // ปลดล็อค clients_mutex

        {
            lock_guard<mutex> lock(rooms_mutex);
            for (auto &r : rooms) {
                result += r.first + "(" + to_string(counts[r.first]) + ") ";
            }
        } // ปลดล็อค rooms_mutex
        // --- (END OK) ---

        send_reply(reply_q, result);
        cout << "[LOG] USER_LIST: " << username << " requested room list.\n";
    }

    // --- 5. CHAT ---
    else if (cmd == "CHAT" && parts.size() >= 5) {
        string room_name = parts[2];
        username = parts[3];
        string message = parts[4];
        update_activity(username);

        bool can_chat = false;
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            if (clients.count(username) && clients[username].current_room == room_name && !room_name.empty()) {
                can_chat = true;
            }
        } //! ปลดล็อค

        if (can_chat) {
            broadcast_to_room(room_name, username, message); // (ใช้เวอร์ชันที่แก้แล้ว)
            {
                lock_guard<mutex> lock(room_mutex);
                room_last_active[room_name] = time(nullptr);
            }
            cout << "[LOG] CHAT_MSG: (" << room_name << ") " << username << ": " << message << "\n";
        } else {
            send_reply(reply_q, "SYSTEM|Error: You must be in a room to chat.");
        }
    }

    // --- 6. WHO ---
    else if (cmd == "WHO" && parts.size() >= 3) {
        username = parts[2];
        update_activity(username);
        string room_name;
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            if (clients.count(username) == 0) return;
            room_name = clients[username].current_room;
        } //! ปลดล็อค

        if (room_name.empty()) {
            send_reply(reply_q, "SYSTEM|Error: You are in the Lobby.");
            return;
        }

        string result = "SYSTEM|Users in " + room_name + ": ";
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            for (auto const& [name, info] : clients) {
                if (info.current_room == room_name) {
                    result += name + " ";
                }
            }
        } //! ปลดล็อค
        send_reply(reply_q, result);
        cout << "[LOG] USER_WHO: " << username << " listed members in " << room_name << ".\n";
    }

    // --- 7. LEAVE ---
    else if (cmd == "LEAVE" && parts.size() >= 3) {
        username = parts[2];
        update_activity(username);
        string old_room;
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            if (clients.count(username) == 0 || clients[username].current_room.empty()) {
                send_reply(reply_q, "SYSTEM|Error: You are already in the Lobby.");
                return;
            }
            old_room = clients[username].current_room;
            clients[username].current_room = "";
        } //! ปลดล็อค

        // --- ‼️ FIX: ย้าย room_mutex มาไว้หลังสุด ‼️ ---

        send_reply(reply_q, "JOIN_SUCCESS|");
        broadcast_to_room(old_room, "SYSTEM", username + " has left the room."); // (ล็อค 1 -> 2)
        cout << "[LOG] ROOM_LEAVE: " << username << " left room '" << old_room << "'.\n";

        // ย้ายมาไว้ตรงนี้ (ล็อค 3)
        // (ต้องเช็คด้วยว่า old_room ไม่ใช่ค่าว่าง)
        if (!old_room.empty()) {
            {
                lock_guard<mutex> lock(room_mutex);
                room_last_active[old_room] = time(nullptr);
            }
        }
        // --- ‼️ END FIX ‼️ ---
    }

    // --- 8. DM ---
    else if (cmd == "DM" && parts.size() >= 5) {
        string target = parts[2];
        string sender = parts[3];
        string message = parts[4];
        update_activity(sender);

        string target_q;
        bool found = false;
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            if (clients.count(target)) {
                target_q = clients[target].reply_queue;
                found = true;
            }
        } //! ปลดล็อค

        if (found) {
            send_reply(target_q, "DM|" + sender + " (DM): " + message);
            send_reply(reply_q, "SYSTEM|DM sent to " + target + ".");
            cout << "[LOG] USER_DM: " << sender << " sent DM to " << target << ".\n";
        } else {
            send_reply(reply_q, "SYSTEM|Error: User " + target + " not found.");
        }
    }

    // --- 9. EXIT ---
    else if (cmd == "EXIT" && parts.size() >= 3) {
        username = parts[2];
        string old_room, user_reply_q;
        bool found = false;
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            if (clients.count(username) == 0) return;
            old_room = clients[username].current_room;
            user_reply_q = clients[username].reply_queue;
            clients.erase(username); // ลบ client ออกจากระบบ
            found = true;
        } //! ปลดล็อค

        if (found) {
            mq_unlink(user_reply_q.c_str()); // ลบคิวของ client (ย้ายมานอก lock)
            broadcast_to_room(old_room, "SYSTEM", username + " has disconnected.");
            send_reply(user_reply_q, "SYSTEM|Goodbye!");
            if (!old_room.empty()) {
                lock_guard<mutex> lock(room_mutex);
                room_last_active[old_room] = time(nullptr);
            }
            cout << "[LOG] USER_EXIT: " << username << " disconnected (Room: " << old_room << ").\n";
        }
    }

    // --- 10. PING ---
    else if (cmd == "PING" && parts.size() >= 3) {
        username = parts[2];
        lock_guard<mutex> lock(hb_mutex);
        last_heartbeat[username] = time(nullptr);
    }

    // --- 11. MEMBERS ---
    else if (cmd == "MEMBERS") {
        string result = "SYSTEM|Online users: ";
        { //! ล็อค (1)
            lock_guard<mutex> lock(clients_mutex);
            for (auto &[name, _] : clients) result += name + " ";
        } //! ปลดล็อค
        send_reply(reply_q, result);
    }

    else {
        send_reply(reply_q, "SYSTEM|Unknown command or invalid format.");
    }
}

// --- ฟังก์ชันที่ Worker Thread แต่ละตัวจะรัน ---
void worker_thread() {
    while (g_server_running) {
        string task;
        {
            unique_lock<mutex> lock(queue_mutex);
            queue_cond.wait(lock, [&]{ return !task_queue.empty() || !g_server_running; });

            if (!g_server_running && task_queue.empty()) {
                break;
            }

            task = task_queue.front();
            task_queue.pop();
        }

        if (!task.empty()) {
            process_message(task);
        }
    }
}

// ------------------------
// MAIN
// ------------------------
int main(int argc, char* argv[]) {
    int num_threads = 1;
    if (argc >= 2) {
        try {
            num_threads = std::stoi(argv[1]);
            if (num_threads < 1) num_threads = 1;
        } catch (const std::exception& e) {
            cerr << "[ERROR] Invalid thread count argument. Using 1." << endl;
            num_threads = 1;
        }
    } else {
        cout << "[Warning] No thread count specified. Defaulting to 1." << endl;
        cout << "Usage: ./server <NumThreads>" << endl;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // --- ตั้งค่า Message Queue ---
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(CONTROL_QUEUE); // ลบคิวเก่า
    mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open server");
        return 1;
    }

    // --- 1. สร้าง Worker Threads ---
    cout << "[Server] Starting " << num_threads << " worker threads..." << endl;
    vector<thread> workers;
    for (int i = 0; i < num_threads; ++i) {
        workers.push_back(thread(worker_thread));
    }

    cout << "[Server] Started. Waiting for clients...\n";

    // --- 2. สร้าง Maintenance Threads ---

    // --- ‼️ FIX 3: แก้ไข Heartbeat Monitor (ลดขอบเขตการล็อค) ---
    thread monitor([](){
        while (g_server_running) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!g_server_running) break;

            time_t now = time(nullptr);
            vector<string> to_remove;
            {
                // 1. ล็อค hb_mutex เพื่อ "อ่าน"
                lock_guard<mutex> lock(hb_mutex);
                for (auto &[user, last] : last_heartbeat) {
                    if (difftime(now, last) > 15) {
                        to_remove.push_back(user);
                    }
                }
            } // ปลดล็อค hb_mutex

            for (auto &user : to_remove) {
                string q, room;
                bool client_found = false;
                {
                    // 2. ล็อค clients_mutex เพื่อ "ลบ"
                    lock_guard<mutex> lock(clients_mutex);
                    if (clients.count(user)) {
                        q = clients[user].reply_queue;
                        room = clients[user].current_room;
                        clients.erase(user);
                        client_found = true;
                    }
                } // ปลดล็อค clients_mutex

                if (client_found) {
                    cout << "[HB] " << user << " timed out (no heartbeat)." << endl;
                    broadcast_to_room(room, "SYSTEM", user + " has disconnected (timeout).");
                    mq_unlink(q.c_str());
                }

                {
                    // 3. ล็อค hb_mutex เพื่อ "ลบ"
                    lock_guard<mutex> lock(hb_mutex);
                    last_heartbeat.erase(user);
                }
            }
        }
    });
    monitor.detach();
    // --- ‼️ END FIX 3 ---

    // --- Room Cleanup Thread (Thread-safe) ---
    // (โค้ดส่วนนี้ OK: ล็อค 1, ปลดล็อค 1, ล็อค 2+room_mutex, ปลดล็อค 2+room_mutex)
    thread room_cleaner([](){
        while (g_server_running) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!g_server_running) break;

            time_t now = time(nullptr);

            map<string, int> counts;
            {
                lock_guard<mutex> lock(clients_mutex);
                for (auto const& [name, info] : clients) {
                    if (!info.current_room.empty()) {
                        counts[info.current_room]++;
                    }
                }
            } // ปลดล็อค clients_mutex

            {
                // ล็อค rooms (2) และ room_mutex
                lock_guard<mutex> lock1(rooms_mutex);
                lock_guard<mutex> lock2(room_mutex);

                vector<string> to_remove;
                for (auto &[room, t] : room_last_active) {
                    if (counts[room] == 0 && difftime(now, t) > 60) {
                        to_remove.push_back(room);
                    }
                }

                for (auto &r : to_remove) {
                    rooms.erase(r);
                    room_last_active.erase(r);
                    cout << "[ROOM CLEANUP] Room '" << r << "' deleted (idle > 60s)\n";
                }
            }
        }
    });
    room_cleaner.detach();

    // --- ‼️ FIX 4: แก้ไข Inactive Kick Thread (เหมือน Monitor) ---
    thread idle_kicker([](){
        while (g_server_running) {
            std::this_thread::sleep_for(std::chrono::seconds(15));
            if (!g_server_running) break;

            time_t now = time(nullptr);
            vector<string> to_kick;
            {
                // 1. ล็อค active_mutex เพื่อ "อ่าน"
                lock_guard<mutex> lock(active_mutex);
                for (auto &[user, t] : last_active) {
                    if (difftime(now, t) > 60) {
                        to_kick.push_back(user);
                    }
                }
            } // ปลดล็อค active_mutex

            for (auto &user : to_kick) {
                string q, room;
                bool client_found = false;
                {
                    // 2. ล็อค clients_mutex เพื่อ "ลบ"
                    lock_guard<mutex> lock(clients_mutex);
                    if (clients.count(user)) {
                        q = clients[user].reply_queue;
                        room = clients[user].current_room;
                        clients.erase(user);
                        client_found = true;
                    }
                } // ปลดล็อค clients_mutex

                if (client_found) {
                    cout << "[INACTIVE KICK] " << user << " disconnected (idle > 60s)\n";
                    send_reply(q, "SYSTEM|You were disconnected due to inactivity.");
                    broadcast_to_room(room, "SYSTEM", user + " has been kicked (inactive).");
                    mq_unlink(q.c_str());
                }

                {
                    // 3. ล็อค active_mutex เพื่อ "ลบ"
                    lock_guard<mutex> lock(active_mutex);
                    last_active.erase(user);
                }
            }
        }
    });
    idle_kicker.detach();
    // --- ‼️ END FIX 4 ---

    // --- 3. Main Loop (Producer) ---
    char buf[MQ_MSGSIZE];
    while (g_server_running) {
        ssize_t bytes = mq_receive(mq, buf, MQ_MSGSIZE, nullptr);

        if (bytes < 0) {
            if (g_server_running && errno != EINTR) perror("[Server ERROR] mq_receive");
            if (!g_server_running) break; // ออกถ้าถูกสั่งปิด
            continue;
        }

        string msg(buf);
        if (msg == "STOP|") {
            break;
        }

        {
            lock_guard<mutex> lock(queue_mutex);
            task_queue.push(msg);
        }
        queue_cond.notify_one();
    }

    // --- 4. Shutdown ---
    cout << "[Server] Stopping... Waiting for workers to finish..." << endl;

    for (thread& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    // --- 5. Cleanup ---
    cout << "[Server] Cleaning up queues..." << endl;
    {
        lock_guard<mutex> lock(clients_mutex);
        for (auto const& [name, info] : clients) {
            mq_unlink(info.reply_queue.c_str());
        }
    }
    mq_close(mq);
    mq_unlink(CONTROL_QUEUE);

    cout << "[Server] Server stopped." << endl;
    return 0;
}

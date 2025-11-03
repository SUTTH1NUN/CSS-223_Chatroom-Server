// docker rm -f chat-server ลบ container เก่า ถ้ามมึงมีนะ
// docker build -t chat-server-mq . สร้าง image
// docker run --name chat-server --privileged -it chat-server-mq รัน container





#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread> // ต้องมีเพื่อใช้ sleep_for
using namespace std;

const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;
mqd_t mq;

// --- โครงสร้างข้อมูลสำหรับติดตามสถานะ ---
struct ClientInfo {
    string username;
    string reply_queue; // คิวส่วนตัวของไคลเอนต์ (เช่น /reply_username_pid)
    string current_room; // ชื่อห้องที่อยู่ (ว่าง = "")
};

struct Room {
    string name;
};

map<string, ClientInfo> clients; // username -> ClientInfo
map<string, Room> rooms;         // room name -> Room struct

// --- Helper Function: ส่งข้อความตอบกลับ ---
void send_reply(const string& reply_q, const string& text) {
    if (reply_q.empty()) return;
    mqd_t client_q = mq_open(reply_q.c_str(), O_WRONLY);
    if (client_q != (mqd_t)-1) {
        mq_send(client_q, text.c_str(), text.size() + 1, 0);
        mq_close(client_q);
    }
}

// --- Helper Function: ส่งข้อความไปยังทุกคนในห้อง ---
void broadcast_to_room(const string& room_name, const string& sender_name, const string& message) {
    if (rooms.count(room_name) == 0) return;
    string full_message = "CHAT|" + sender_name + ": " + message;

    for (auto const& [name, info] : clients) {
        if (info.current_room == room_name && name != sender_name) {
            send_reply(info.reply_queue, full_message);
        }
    }
}

// --- Helper Function: นับสมาชิกในห้อง ---
int count_members_in_room(const string& room_name) {
    int count = 0;
    for (auto const& [name, info] : clients) {
        if (info.current_room == room_name) {
            count++;
        }
    }
    return count;
}


// ------------------------
// Signal Handler และ Main Loop
// ------------------------
void handle_sigint(int) {
    cout << "\n[Server] Caught SIGINT, cleaning up..." << endl;
    for (auto const& [name, info] : clients) {
        mq_unlink(info.reply_queue.c_str());
    }
    mq_close(mq);
    mq_unlink(CONTROL_QUEUE);
    cout << "[Server] Queue removed. Exiting.\n";
    exit(0);
}

int main() {
    signal(SIGINT, handle_sigint);

    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(CONTROL_QUEUE);
    mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open server");
        return 1;
    }

    cout << "[Server] Started. Waiting for clients...\n";

    char buf[MQ_MSGSIZE];

    while (true) {
        ssize_t bytes = mq_receive(mq, buf, MQ_MSGSIZE, nullptr);
        if (bytes < 0) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        string msg(buf);
        vector<string> parts;
        stringstream ss(msg);
        string token;
        while (getline(ss, token, '|')) parts.push_back(token);
        if (parts.empty() || parts.size() < 2) continue;

        string cmd = parts[0];
        string reply_q = parts[1];
        string username = (parts.size() >= 3) ? parts[2] : "";

        // --- 1. REGISTER ---
        if (cmd == "REGISTER" && parts.size() >= 3) {
            username = parts[2];
            if (clients.count(username)) {
                send_reply(reply_q, "SYSTEM|Error: Username already taken.");
            } else {
                clients[username] = {username, reply_q, ""};
                send_reply(reply_q, "SYSTEM|Welcome " + username + "! You are in the Lobby.");
                cout << "[LOG] USER_REG: " << username << " registered (Q: " << reply_q << ")\n";
            }
        }

        // --- 2. CREATE ---
        else if (cmd == "CREATE" && parts.size() >= 4) {
            string room_name = parts[2];
            username = parts[3];
            if (clients.count(username) == 0 || rooms.count(room_name)) {
                string err = rooms.count(room_name) ? "Room already exists: " : "User not registered.";
                send_reply(reply_q, "SYSTEM|Error: " + err + room_name);
            } else {
                rooms[room_name] = Room{room_name};
                clients[username].current_room = room_name;
                send_reply(reply_q, "JOIN_SUCCESS|" + room_name);
                cout << "[LOG] ROOM_CREATE: " << username << " created and joined room '" << room_name << "'.\n";
            }
        }

        // --- 3. JOIN ---
        else if (cmd == "JOIN" && parts.size() >= 4) {
            string room_name = parts[2];
            username = parts[3];
            if (rooms.count(room_name) && clients.count(username)) {
                string old_room = clients[username].current_room;
                clients[username].current_room = room_name;
                
                send_reply(reply_q, "JOIN_SUCCESS|" + room_name); 
                broadcast_to_room(room_name, "SYSTEM", username + " has joined.");
                cout << "[LOG] ROOM_JOIN: " << username << " joined room '" << room_name << "' (from: " << old_room << ").\n";
            } else {
                send_reply(reply_q, "SYSTEM|Error: Room or user not found.");
            }
        }
        
        // --- 4. LIST (แก้ไขการนับสมาชิก) ---
        else if (cmd == "LIST" && parts.size() >= 3) {
            username = parts[2]; // ผู้ใช้ที่ขอรายการ
            string result = "LIST|Available Rooms: ";
            for (auto &r : rooms) {
                int count = count_members_in_room(r.first);
                result += r.first + "(" + to_string(count) + ") "; 
            }
            send_reply(reply_q, result);
            cout << "[LOG] USER_LIST: " << username << " requested room list.\n";
        }

        // --- 5. CHAT ---
        else if (cmd == "CHAT" && parts.size() >= 5) {
            string room_name = parts[2];
            username = parts[3];
            string message = parts[4];
            
            if (clients.count(username) && clients[username].current_room == room_name && !room_name.empty()) {
                broadcast_to_room(room_name, username, message);
                cout << "[LOG] CHAT_MSG: (" << room_name << ") " << username << ": " << message << "\n";
            } else {
                 send_reply(reply_q, "SYSTEM|Error: You must be in a room to chat.");
            }
        }
        
        // --- 6. WHO ---
        else if (cmd == "WHO" && parts.size() >= 3) {
            username = parts[2];
            if (clients.count(username) == 0) continue;
            string room_name = clients[username].current_room;
            
            if (room_name.empty()) {
                send_reply(reply_q, "SYSTEM|Error: You are in the Lobby.");
                continue;
            }

            string result = "SYSTEM|Users in " + room_name + ": ";
            for (auto const& [name, info] : clients) {
                if (info.current_room == room_name) {
                    result += name + " ";
                }
            }
            send_reply(reply_q, result);
            cout << "[LOG] USER_WHO: " << username << " listed members in " << room_name << ".\n";
        }

        // --- 7. LEAVE (ออกจากห้อง) ---
        else if (cmd == "LEAVE" && parts.size() >= 3) {
            username = parts[2];
            if (clients.count(username) && !clients[username].current_room.empty()) {
                string old_room = clients[username].current_room;
                clients[username].current_room = ""; // กลับไป Lobby
                send_reply(reply_q, "JOIN_SUCCESS|"); // ส่งค่าว่างเพื่อกลับ Lobby
                broadcast_to_room(old_room, "SYSTEM", username + " has left the room.");
                cout << "[LOG] ROOM_LEAVE: " << username << " left room '" << old_room << "'.\n";
            } else {
                 send_reply(reply_q, "SYSTEM|Error: You are already in the Lobby.");
            }
        }
        
        // --- 8. DM (Direct Message) ---
        else if (cmd == "DM" && parts.size() >= 5) {
            string target = parts[2];
            string sender = parts[3];
            string message = parts[4];
            
            if (clients.count(target)) {
                string target_q = clients[target].reply_queue;
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
            if (clients.count(username)) {
                string current_room = clients[username].current_room;
                
                broadcast_to_room(current_room, "SYSTEM", username + " has disconnected.");

                send_reply(reply_q, "SYSTEM|Goodbye!");
                
                mq_unlink(clients[username].reply_queue.c_str());
                clients.erase(username);
                
                cout << "[LOG] USER_EXIT: " << username << " disconnected (Room: " << current_room << ").\n";
            }
        }
        else {
             send_reply(reply_q, "SYSTEM|Unknown command or invalid format.");
        }
    }

    mq_close(mq);
    mq_unlink(CONTROL_QUEUE);
    return 0;
}

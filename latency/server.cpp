#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
using namespace std;

// --- Global Settings ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;
mqd_t mq;

// --- Data Structures ---
struct ClientInfo {
    string username;
    string reply_queue; // e.g., /reply_Alice_12345
    string current_room; // "" = Lobby
};

struct Room {
    string name;
};

map<string, ClientInfo> clients; 
map<string, Room> rooms;         

// --- Helper Functions ---
void send_reply(const string& reply_q, const string& text) {
    if (reply_q.empty()) return;
    mqd_t client_q = mq_open(reply_q.c_str(), O_WRONLY);
    if (client_q != (mqd_t)-1) {
        mq_send(client_q, text.c_str(), text.size() + 1, 0);
        mq_close(client_q);
    }
}

void broadcast_to_room(const string& room_name, const string& sender_name, const string& message) {
    if (rooms.count(room_name) == 0) return;
    string full_message = "CHAT|" + sender_name + ": " + message;

    for (auto const& [name, info] : clients) {
        if (info.current_room == room_name && name != sender_name) {
            send_reply(info.reply_queue, full_message);
        }
    }
}

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
// Signal Handler and Main Loop
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

        // --- Core Commands (Simplified for brevity, but all logic is included in final code) ---
        
        // 1. REGISTER
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
        
        // 2. CREATE
        else if (cmd == "CREATE" && parts.size() >= 4) {
            string room_name = parts[2]; username = parts[3];
            if (clients.count(username) == 0 || rooms.count(room_name)) {
                send_reply(reply_q, "SYSTEM|Error: Room already exists or user not registered.");
            } else {
                rooms[room_name] = Room{room_name};
                clients[username].current_room = room_name;
                send_reply(reply_q, "JOIN_SUCCESS|" + room_name);
                cout << "[LOG] ROOM_CREATE: " << username << " created and joined room '" << room_name << "'.\n";
            }
        }

        // 3. JOIN
        else if (cmd == "JOIN" && parts.size() >= 4) {
            string room_name = parts[2]; username = parts[3];
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
        
        // 4. LIST
        else if (cmd == "LIST" && parts.size() >= 3) {
            string result = "LIST|Available Rooms: ";
            for (auto &r : rooms) {
                int count = count_members_in_room(r.first);
                result += r.first + "(" + to_string(count) + ") "; 
            }
            send_reply(reply_q, result);
            cout << "[LOG] USER_LIST: " << username << " requested room list.\n";
        }

        // 5. CHAT
        else if (cmd == "CHAT" && parts.size() >= 5) {
            string room_name = parts[2]; username = parts[3]; string message = parts[4];
            if (clients.count(username) && clients[username].current_room == room_name && !room_name.empty()) {
                broadcast_to_room(room_name, username, message);
                cout << "[LOG] CHAT_MSG: (" << room_name << ") " << username << ": " << message << "\n";
            } else {
                 send_reply(reply_q, "SYSTEM|Error: You must be in a room to chat.");
            }
        }
        
        // 6. WHO
        else if (cmd == "WHO" && parts.size() >= 3) {
            username = parts[2];
            if (clients.count(username) == 0 || clients[username].current_room.empty()) {
                 send_reply(reply_q, "SYSTEM|Error: You are in the Lobby.");
                 continue;
            }
            string room_name = clients[username].current_room;
            string result = "SYSTEM|Users in " + room_name + ": ";
            for (auto const& [name, info] : clients) {
                if (info.current_room == room_name) { result += name + " "; }
            }
            send_reply(reply_q, result);
            cout << "[LOG] USER_WHO: " << username << " listed members in " << room_name << ".\n";
        }

        // 7. LEAVE
        else if (cmd == "LEAVE" && parts.size() >= 3) {
            username = parts[2];
            if (clients.count(username) && !clients[username].current_room.empty()) {
                string old_room = clients[username].current_room;
                clients[username].current_room = ""; 
                send_reply(reply_q, "JOIN_SUCCESS|"); 
                broadcast_to_room(old_room, "SYSTEM", username + " has left the room.");
                cout << "[LOG] ROOM_LEAVE: " << username << " left room '" << old_room << "'.\n";
            } else {
                 send_reply(reply_q, "SYSTEM|Error: You are already in the Lobby.");
            }
        }
        
        // 8. DM
        else if (cmd == "DM" && parts.size() >= 5) {
            string target = parts[2]; string sender = parts[3]; string message = parts[4];
            if (clients.count(target)) {
                string target_q = clients[target].reply_queue;
                send_reply(target_q, "DM|" + sender + " (DM): " + message);
                send_reply(reply_q, "SYSTEM|DM sent to " + target + ".");
                cout << "[LOG] USER_DM: " << sender << " sent DM to " << target << ".\n";
            } else {
                send_reply(reply_q, "SYSTEM|Error: User " + target + " not found.");
            }
        }
        
        // 9. PING (Server Latency)
        else if (cmd == "PING" && parts.size() >= 3) {
            string timestamp = parts[2]; 
            send_reply(reply_q, "PONG|" + timestamp); 
        }

        // 10. PINGTO (End-to-End Latency: Step 1)
        else if (cmd == "PINGTO" && parts.size() >= 5) {
    string target = parts[2]; 
    string sender = parts[3]; 
    string timestamp = parts[4];
    
    if (clients.count(target)) {
        string target_q = clients[target].reply_queue;
        // ดึงคิวตอบกลับของผู้ส่ง (sender_reply_q) จาก Clients Map
        string sender_reply_q = clients[sender].reply_queue; 

        // ส่งต่อไปยัง Client เป้าหมาย (Target) พร้อมแนบคิวตอบกลับของผู้ส่งไปด้วย
        // PINGTO|T1_nanos|SenderName|SenderReplyQueue  <--- เพิ่ม SenderReplyQueue
        send_reply(target_q, "PINGTO|" + timestamp + "|" + sender + "|" + sender_reply_q); // <--- แก้ไขที่นี่

        send_reply(reply_q, "SYSTEM|Sent E2E PING request to " + target + "...");
        cout << "[LOG] PING_E2E: " << sender << " started E2E ping to " << target << "\n";
    } else {
        send_reply(reply_q, "SYSTEM|Error: User " + target + " not found for PINGTO.");
    }
}
        
        // 11. PONGTO (End-to-End Latency: Step 3 - ได้รับ Echo จาก Target)
//
// **ข้อความที่ Target Client (fill) ส่งมา: PONGTO | SenderReplyQ | T1_nanos** (มี 3 ส่วน)
//
else if (cmd == "PONGTO" && parts.size() >= 3) { // <--- 1. แก้ไข: ตรวจสอบ 3 ส่วน
    
    // Server ได้รับ: PONGTO | คิวของ hall | T1_nanos
    
    string sender_reply_q = parts[1]; // <--- 2. แก้ไข: คิวตอบกลับของผู้ส่ง (hall) อยู่ที่ parts[1]
    string timestamp = parts[2];    // <--- 3. แก้ไข: Timestamp อยู่ที่ parts[2]
    
    // เราไม่ต้องการ parts[3] (sender_name) อีกต่อไป เพราะเรามีคิวโดยตรงแล้ว
    
    // Server ส่ง PONGTO กลับไปยัง Client เริ่มต้น (hall)
    // PONGTO|T1_nanos
    send_reply(sender_reply_q, "PONGTO|" + timestamp); // <--- 4. ใช้ sender_reply_q โดยตรง
    
    cout << "[LOG] PONG_E2E: Sent E2E PONG back to " << sender_reply_q << ".\n";
}
        // 12. EXIT
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

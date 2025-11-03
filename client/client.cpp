// docker cp client.cpp chat-server:/app/ copy ไฟล์ client เข้า container server
// docker exec -it chat-server bash เข้าไปใน container server
// ./chat_client 





#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <thread>
#include <atomic>

using namespace std;

// --- Queue Settings (ต้องตรงกับ Server) ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;

// --- Global State ---
atomic<bool> g_running(true);
string g_myName;
string g_clientQueueName;
string g_currentRoom = ""; // "" = Lobby

// --- Prototypes (แก้ไข Compile Error) ---
void receiverThread();
bool sendCommand(const string& cmd, const string& payload = "");
void showPrompt(); // <--- ประกาศล่วงหน้า

// ------------------------
// Thread รับข้อความจาก Server
// ------------------------
void receiverThread() {
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    
    mq_unlink(g_clientQueueName.c_str());
    mqd_t my_mq = mq_open(g_clientQueueName.c_str(), O_CREAT | O_RDONLY, 0666, &attr);

    if (my_mq == (mqd_t)-1) {
        cerr << "[CLIENT ERROR] Failed to open client queue " << g_clientQueueName << endl;
        g_running = false;
        return;
    }
    
    char buf[MQ_MSGSIZE];
    
    while (g_running) {
        ssize_t bytes = mq_receive(my_mq, buf, MQ_MSGSIZE, nullptr);
        
        if (bytes > 0) {
            string response(buf);
            
            size_t sep = response.find('|');
            string type = (sep == string::npos) ? "" : response.substr(0, sep);
            string message = (sep == string::npos) ? response : response.substr(sep + 1);

            cout << "\n";
            
            if (type == "SYSTEM") {
                cout << "[SYSTEM] " << message;
            } else if (type == "LIST") {
                cout << "[ROOMS] " << message;
            } else if (type == "CHAT") {
                cout << "[" << g_currentRoom << "] " << message;
            } else if (type == "DM") {
                cout << "[DM] " << message;
            } else if (type == "JOIN_SUCCESS") {
                g_currentRoom = message;
                if (message.empty()) {
                     cout << "[SYSTEM] Returned to Lobby.";
                } else {
                     cout << "[SYSTEM] Successfully joined room '" << message << "'.";
                }
            } else {
                cout << "[RAW] " << response;
            }
            
            cout << endl;
            showPrompt();
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    
    mq_close(my_mq);
    mq_unlink(g_clientQueueName.c_str());
}

// ------------------------
// ฟังก์ชันส่งคำสั่งไปยัง Server
// ------------------------
bool sendCommand(const string& cmd, const string& payload) {
    mqd_t server_mq = mq_open(CONTROL_QUEUE, O_WRONLY | O_NONBLOCK);
    if (server_mq == (mqd_t)-1) {
        if (errno == ENOENT) {
            cerr << "[ERROR] Server queue not found. Server might not be running.\n";
        } else if (errno == ENXIO) {
            cerr << "[ERROR] No server process currently listening on queue.\n";
        } else {
            perror("[CLIENT ERROR] mq_open control queue");
        }
        g_running = false;
        exit(EXIT_FAILURE);
    }

    string message = cmd + "|" + g_clientQueueName + payload;
    if (mq_send(server_mq, message.c_str(), message.size() + 1, 0) == -1) {
        perror("[CLIENT ERROR] mq_send");
        mq_close(server_mq);
        exit(EXIT_FAILURE);
    }

    mq_close(server_mq);
    return true;
}

// ------------------------
// แสดง Prompt
// ------------------------
void showPrompt() {
    if (g_currentRoom.empty()) {
        cout << "[Lobby] > ";
    } else {
        cout << "[" << g_currentRoom << "] > ";
    }
    cout.flush();
}

// ------------------------
// MAIN
// ------------------------
int main() {
    cout << "Enter your name: ";
    getline(cin, g_myName);
    
    g_clientQueueName = "/reply_" + g_myName + "_" + to_string(getpid());
    
    if (!sendCommand("REGISTER", "|" + g_myName)) {
        cerr << "[ERROR] Could not register. Server might be down.\n";
        return 1;
    }

    thread receiver(receiverThread);
    
    thread heartbeat([](){
        while (g_running) {
            this_thread::sleep_for(chrono::seconds(5)); // ส่งทุก 5 วิ
            if (!g_running) break;
            sendCommand("PING", "|" + g_myName);
        }
    }); 

    this_thread::sleep_for(chrono::milliseconds(100));

    cout << "\n--- Commands ---\n";
    cout << " /list               - Show all Rooms and member counts\n";
    cout << " /create <room>      - Create and join room\n";
    cout << " /join <room>        - Join Room\n";
    cout << " /leave              - Leave current room and return to Lobby\n";
    cout << " /who                - Show users in current room\n";
    cout << " /dm <name> <msg>    - Send Direct Message\n";
    cout << " /exit               - Disconnect and Quit\n";
    cout << " (Type message to chat in room)\n";
    cout << "----------------\n\n";

    string input;
    while (g_running) {
        showPrompt();
        getline(cin, input);
        
        if (!g_running) break;
        if (input.empty()) continue;

        // ตรวจสอบว่าเป็นคำสั่ง (ขึ้นต้นด้วย /) หรือไม่
        if (input[0] == '/') {
            string cmd;
            stringstream ss(input);
            ss >> cmd;
            
            // --- คำสั่งเฉพาะ ---
            if (cmd == "/exit") {
                sendCommand("EXIT", "|" + g_myName);
                g_running = false; // สั่งปิดตัว (Server จะส่ง Goodbye ตามมา)
                break;
            }
            else if (cmd == "/list") {
                sendCommand("LIST", "|" + g_myName);
            }
            else if (cmd == "/who") {
                 if (g_currentRoom.empty()) {
                    cout << "[ERROR] You must be in a room to use /who.\n";
                } else {
                    sendCommand("WHO", "|" + g_myName);
                }
            }
            else if (cmd == "/leave") {
                 if (g_currentRoom.empty()) {
                    cout << "[ERROR] You are already in the Lobby.\n";
                } else {
                    sendCommand("LEAVE", "|" + g_myName);
                }
            }
            else if (cmd == "/create" || cmd == "/join") {
                string room; ss >> room;
                if (room.empty()) {
                    cout << "[ERROR] Usage: " << cmd << " <room_name>\n";
                } else {
                    string payload_cmd = (cmd == "/create") ? "CREATE" : "JOIN";
                    sendCommand(payload_cmd, "|" + room + "|" + g_myName);
                }
            }
            else if (cmd == "/dm") {
                string target, msg_part;
                ss >> target;
                if (target.empty() || !(ss >> ws && getline(ss, msg_part))) {
                     cout << "[ERROR] Usage: /dm <name> <message>\n";
                } else {
                    // DM|REPLY_Q|TARGET_USERNAME|SENDER_USERNAME|MESSAGE
                    sendCommand("DM", "|" + target + "|" + g_myName + "|" + msg_part);
                }
            }
            else if (cmd == "/members") {
                sendCommand("MEMBERS", "|" + g_myName);
            }
            else {
                 cout << "[ERROR] Unknown command: " << cmd << endl;
            }
        }
        // --- ข้อความแชท (ถ้าไม่ขึ้นต้นด้วย /) ---
        else {
             if (g_currentRoom.empty()) {
                cout << "[ERROR] You must be in a room to chat. Use /create or /join.\n";
            } else {
                // CHAT|REPLY_Q|ROOM_NAME|USERNAME|MESSAGE (ใช้ input ทั้งหมดเป็น Message)
                sendCommand("CHAT", "|" + g_currentRoom + "|" + g_myName + "|" + input);
            }
        }
    }
    
    g_running = false;
    if (receiver.joinable()) receiver.join();
    if (heartbeat.joinable()) heartbeat.join();
    
    cout << "\n[CLIENT] Disconnected" << endl;
    return 0;
}

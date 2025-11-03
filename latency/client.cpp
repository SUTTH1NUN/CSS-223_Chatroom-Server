#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <thread>
#include <atomic>
#include <chrono> 
#include <iomanip> 

using namespace std;

// --- Global Settings ---
const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;

// --- Global State ---
atomic<bool> g_running(true);
string g_myName;
string g_clientQueueName;
string g_currentRoom = ""; 

// --- Prototypes ---
void receiverThread();
bool sendCommand(const string& cmd, const string& payload = "");
void showPrompt(); 
string get_time_string();

// --- Helper for Latency ---
string get_time_string() {
    auto now = chrono::high_resolution_clock::now();
    return to_string(now.time_since_epoch().count());
}

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
            
            // --- PINGTO (ถูกเรียกให้ Echo กลับ) ---
            if (type == "PINGTO") { 
    vector<string> parts;
    stringstream ss(message);
    string token;
    while (getline(ss, token, '|')) parts.push_back(token);
    
    string T1_nanos = parts[0];
    string sender_name = parts[1];
    string sender_reply_q = parts[2]; // <--- รับคิวตอบกลับของผู้ส่ง (hall)

    // PONGTO|REPLY_Q_Sender|T1_nanos (ส่งกลับไปที่ Server)
    mqd_t server_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (server_mq != (mqd_t)-1) {
        // PONGTO | คิวตอบกลับของ Sender | T1_nanos
        // Server จะรับ PONGTO แล้วส่งต่อไปยังคิวตอบกลับที่อยู่ใน payload
        string reply_msg = "PONGTO|" + sender_reply_q + "|" + T1_nanos; // <--- แก้ไข: ใช้คิวของผู้ส่งโดยตรง
        mq_send(server_mq, reply_msg.c_str(), reply_msg.size() + 1, 0);
        mq_close(server_mq);
    }
    
    cout << "\n[ECHO] Responded to PINGTO from " << sender_name << endl;
    showPrompt();

}
            // --- PONGTO (ได้รับผลลัพธ์ End-to-End) ---
            else if (type == "PONGTO") { 
                auto T2 = chrono::high_resolution_clock::now();
                long long T1_nanos = stoll(message); 
                long long T2_nanos = T2.time_since_epoch().count();
                
                long long latency_nanos = T2_nanos - T1_nanos;
                double latency_ms = (double)latency_nanos / 1000000.0;
                
                cout << "[LATENCY-E2E] RTT (4-way): " << fixed << setprecision(3) << latency_ms << " ms";

            }
            // --- PONG (Server Latency) ---
            else if (type == "PONG") {
                auto T2 = chrono::high_resolution_clock::now();
                long long T1_nanos = stoll(message); 
                long long T2_nanos = T2.time_since_epoch().count();
                long long latency_nanos = T2_nanos - T1_nanos;
                double latency_ms = (double)latency_nanos / 1000000.0;
                cout << "[LATENCY-SVR] RTT (2-way): " << fixed << setprecision(3) << latency_ms << " ms";
            }
            // --- Standard Messages ---
            else if (type == "SYSTEM") {
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
            }
            else {
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
    mqd_t server_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (server_mq == (mqd_t)-1) {
        perror("[CLIENT ERROR] mq_open control queue");
        g_running = false;
        return false;
    }

    string message = cmd + "|" + g_clientQueueName + payload;
    
    if (mq_send(server_mq, message.c_str(), message.size() + 1, 0) == -1) {
        perror("[CLIENT ERROR] mq_send");
        mq_close(server_mq);
        return false;
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
    
    this_thread::sleep_for(chrono::milliseconds(100));

    cout << "\n--- Commands ---\n";
    cout << " /list               - Show all Rooms and member counts\n";
    cout << " /create <room>      - Create and join room\n";
    cout << " /join <room>        - Join Room\n";
    cout << " /leave              - Leave current room and return to Lobby\n";
    cout << " /who                - Show users in current room\n";
    cout << " /dm <name> <msg>    - Send Direct Message\n";
    cout << " /ping               - Test Server Latency (2-way)\n";
    cout << " /pingto <name>      - Test End-to-End Latency (4-way) (NEW!)\n";
    cout << " /exit               - Disconnect and Quit\n";
    cout << " (Type message to chat in room)\n";
    cout << "----------------\n\n";

    string input;
    while (g_running) {
        showPrompt();
        getline(cin, input);
        
        if (!g_running) break;
        if (input.empty()) continue;

        if (input[0] == '/') {
            string cmd;
            stringstream ss(input);
            ss >> cmd;
            
            // --- คำสั่งเฉพาะ ---
            if (cmd == "/exit") {
                sendCommand("EXIT", "|" + g_myName);
                g_running = false; 
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
                    sendCommand("DM", "|" + target + "|" + g_myName + "|" + msg_part);
                }
            }
            else if (cmd == "/ping") { 
                string T1 = get_time_string();
                sendCommand("PING", "|" + T1);
            }
            else if (cmd == "/pingto") { // NEW!
                string target; ss >> target;
                if (target.empty() || target == g_myName) {
                     cout << "[ERROR] Usage: /pingto <target_name> (must be another user).\n";
                } else {
                    string T1 = get_time_string();
                    sendCommand("PINGTO", "|" + target + "|" + g_myName + "|" + T1);
                }
            }
            else {
                 cout << "[ERROR] Unknown command: " << cmd << endl;
            }
        }
        // --- ข้อความแชท ---
        else {
             if (g_currentRoom.empty()) {
                cout << "[ERROR] You must be in a room to chat. Use /create or /join.\n";
            } else {
                sendCommand("CHAT", "|" + g_currentRoom + "|" + g_myName + "|" + input);
            }
        }
    }
    
    if (receiver.joinable()) {
        receiver.join();
    }
    
    cout << "\n[CLIENT] Disconnected" << endl;
    return 0;
}

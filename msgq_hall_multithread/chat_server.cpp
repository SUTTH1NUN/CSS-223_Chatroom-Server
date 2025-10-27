// Complie : g++ -std=c++17 chat_server.cpp -o chat_server.exe -luser32 -lgdi32 -static

#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <thread>         // สำหรับ Worker
#include <shared_mutex>   // สำหรับ Reader-Writer Lock
#include "SafeQueue.h"  // คิวงาน

// --- ชื่อหน้าต่าง ---
const char* SERVER_CLASS = "ChatServerWindowClass";
const char* SERVER_TITLE = "Chat Server";

// --- Protocol (Client to Server) ---
#define CMD_REGISTER     100 // lParam: "ClientName"
#define CMD_JOIN_ROOM    101 // lParam: "RoomName"
#define CMD_CHAT_MSG     102 // lParam: "Hello world"
#define CMD_LIST_ROOMS   103 // lParam: NULL
#define CMD_DISCONNECT   104 // lParam: NULL
#define CMD_LEAVE_ROOM   105 // lParam: NULL
#define CMD_WHO          106 // lParam: NULL
#define CMD_DM           107 // lParam: "targetName message"
#define CMD_CREATE_ROOM  108 // (เพิ่มใหม่) lParam: "RoomName"

// --- Protocol (Server to Client) ---
#define RSP_CHAT_MSG     200 // lParam: "[Room] User: Hello"
#define RSP_SYSTEM_MSG   201 // lParam: "[SYSTEM] User has joined."
#define RSP_ROOM_LIST    202 // lParam: "Rooms:\n - main\n"
#define RSP_JOIN_SUCCESS 203 // lParam: "RoomName" (ถ้า "" = Lobby)
#define RSP_ERROR        204 // lParam: "Name already taken."

// --- Server State (Global) ---
struct ClientInfo {
    std::string name;
    std::string current_room;
};
std::map<HWND, ClientInfo> g_clients;
std::map<std::string, std::vector<HWND>> g_rooms;

// --- Concurrency Primitives ---
std::shared_mutex g_registries_lock; // Reader-Writer Lock สำหรับ g_clients และ g_rooms
SafeQueue         g_job_queue;         // (แก้ไข) คิวงานสำหรับ Router -> Worker

// --- Prototypes ---
LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam); // นี่คือ Router
void WorkerLoop(int workerId); // นี่คือ Worker
void HandleDisconnect(HWND clientHwnd);
void SendToClient(HWND clientHwnd, DWORD responseType, const std::string& message);
void BroadcastToRoom(const std::string& roomName, DWORD responseType, const std::string& message, HWND excludeHwnd = NULL);
void HandleRegister(HWND clientHwnd, const char* name);
void HandleJoinRoom(HWND clientHwnd, const char* roomName);
void HandleLeaveRoom(HWND clientHwnd);
void HandleChatMessage(HWND clientHwnd, const char* message);
void HandleListRooms(HWND clientHwnd);
void HandleWho(HWND clientHwnd); 
void HandleDm(HWND clientHwnd, const char* payload);
void HandleCreateRoom(HWND clientHwnd, const char* roomName); // (เพิ่มใหม่)

// --- Main ---
int main() {
    WNDCLASS wc = {};
    wc.lpfnWndProc = ServerWndProc; // <-- Main Thread นี้จะทำหน้าที่เป็น Router
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = SERVER_CLASS;

    if (!RegisterClass(&wc)) {
        std::cerr << "[ERROR] Cannot register window class.\n";
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        0, SERVER_CLASS, SERVER_TITLE, 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL
    );

    if (hwnd == NULL) {
        std::cerr << "[ERROR] Failed to create server window.\n";
        return 1;
    }

    std::cout << "==================================\n";
    std::cout << "  Chat Server Started (Router/Worker)\n";
    std::cout << "==================================\n";

    // --- สร้าง Worker Pool ---
    const int NUM_WORKERS = 4; 
    std::vector<std::thread> worker_pool;
    std::cout << "[INFO] Starting " << NUM_WORKERS << " worker threads...\n";
    for (int i = 0; i < NUM_WORKERS; ++i) {
        worker_pool.emplace_back(WorkerLoop, i);
    }
    std::cout << "[INFO] Router (Main Thread) waiting for clients...\n";

    // --- Message loop (Router Thread) ---
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg); // ส่งไปที่ ServerWndProc
    }

    // (Shutdown)
    for (auto& th : worker_pool) {
        if (th.joinable()) {
            th.join(); 
        }
    }
    return 0;
}

// --- Worker Thread Loop ---
void WorkerLoop(int workerId) {
    std::cout << "[WORKER " << workerId << "] Started and waiting for jobs.\n";
    while (true) {
        Job job = g_job_queue.pop(); // ดึงงาน (ถ้าไม่มีจะหลับรอ)
        
        // ตรวจสอบว่า Client ยังอยู่ไหม (ยกเว้นตอน Register)
        if (job.commandType != CMD_REGISTER) {
            std::shared_lock<std::shared_mutex> lock(g_registries_lock);
            if (g_clients.find(job.senderHwnd) == g_clients.end()) {
                std::cerr << "[WORKER " << workerId << "] Job from unregistered/disconnected client " << job.senderHwnd << ". Ignoring.\n";
                continue;
            }
        }

        switch (job.commandType) {
            case CMD_REGISTER:
                HandleRegister(job.senderHwnd, job.payload.c_str());
                break;
            case CMD_JOIN_ROOM:
                HandleJoinRoom(job.senderHwnd, job.payload.c_str());
                break;
            case CMD_CHAT_MSG:
                HandleChatMessage(job.senderHwnd, job.payload.c_str());
                break;
            case CMD_LIST_ROOMS:
                HandleListRooms(job.senderHwnd);
                break;
            case CMD_DISCONNECT:
                HandleDisconnect(job.senderHwnd);
                break;
            case CMD_LEAVE_ROOM:
                HandleLeaveRoom(job.senderHwnd);
                break;
            case CMD_WHO:
                HandleWho(job.senderHwnd);
                break;
            case CMD_DM:
                HandleDm(job.senderHwnd, job.payload.c_str());
                break;
            case CMD_CREATE_ROOM: // (เพิ่มใหม่)
                HandleCreateRoom(job.senderHwnd, job.payload.c_str());
                break;
            default:
                std::cerr << "[WARN] Worker received unknown command: " << job.commandType << "\n";
        }
    }
}

// --- Window Procedure (Router Thread) ---
LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COPYDATA: {
            HWND senderHwnd = (HWND)wParam;
            COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;

            Job job;
            job.senderHwnd = senderHwnd;
            job.commandType = cds->dwData;
            if (cds->cbData > 0 && cds->lpData) {
                job.payload = std::string((const char*)cds->lpData);
            }

            g_job_queue.push(std::move(job));
            
            return 1; // บอก Client ว่ารับทราบ
        }
        case WM_DESTROY:
            std::cout << "[INFO] Server shutting down...\n";
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


// --- Helper Functions (ต้อง Lock ทั้งหมด) ---

void SendToClient(HWND clientHwnd, DWORD responseType, const std::string& message) {
    if (!IsWindow(clientHwnd)) {
        return; 
    }
    COPYDATASTRUCT cds;
    cds.dwData = responseType;
    cds.cbData = (DWORD)message.length() + 1;
    cds.lpData = (PVOID)message.c_str();
    SendMessageTimeout(clientHwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds, SMTO_NORMAL, 1000, NULL);
}

// (แก้ไข) ลบ default argument
void BroadcastToRoom(const std::string& roomName, DWORD responseType, const std::string& message, HWND excludeHwnd) {
    std::shared_lock<std::shared_mutex> lock(g_registries_lock);
    
    if (g_rooms.find(roomName) == g_rooms.end()) return;

    std::vector<HWND> clientsInRoom = g_rooms.at(roomName); // Copy list ออกมา
    lock.unlock(); // ปลดล็อคเร็วที่สุด

    for (HWND clientHwnd : clientsInRoom) {
        if (clientHwnd == excludeHwnd) continue;
        SendToClient(clientHwnd, responseType, message);
    }
}

// "Write" Lock (ต้องใช้ unique_lock)
void HandleRegister(HWND clientHwnd, const char* name) {
    std::unique_lock<std::shared_mutex> lock(g_registries_lock);
    
    std::string sName(name);
    for (auto const& [hwnd, info] : g_clients) {
        if (info.name == sName) {
            lock.unlock(); // ปลดล็อคก่อนส่ง
            SendToClient(clientHwnd, RSP_ERROR, "Error: Name '" + sName + "' is already taken.");
            return;
        }
    }
    g_clients[clientHwnd] = { sName, "" };
    lock.unlock(); // ปลดล็อคก่อนส่ง

    std::cout << "[INFO] Client registered: " << clientHwnd << " as '" << sName << "'\n";
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, ""); 
}

// (แก้ไข) "Write" Lock - Join-Only Logic
void HandleJoinRoom(HWND clientHwnd, const char* roomName) {
    std::unique_lock<std::shared_mutex> lock(g_registries_lock);

    std::string sRoomName(roomName);
    if (sRoomName.empty()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: Room name cannot be empty.");
        return;
    }

    // 1. ตรวจสอบว่าห้อง *มีอยู่จริง* หรือไม่
    if (g_rooms.find(sRoomName) == g_rooms.end()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: Room '" + sRoomName + "' not found. Use /create to make it.");
        return;
    }

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string oldRoom = clientInfo.current_room;

    if (oldRoom == sRoomName) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are already in this room.");
        return;
    }

    std::string clientName = clientInfo.name;
    std::string leaveMsg;
    
    // 2. ออกจากห้องเก่า (ถ้ามี)
    if (!oldRoom.empty() && g_rooms.find(oldRoom) != g_rooms.end()) {
        std::vector<HWND>& clientsInOldRoom = g_rooms[oldRoom];
        clientsInOldRoom.erase(std::remove(clientsInOldRoom.begin(), clientsInOldRoom.end(), clientHwnd), clientsInOldRoom.end());
        leaveMsg = "[SYSTEM] " + clientName + " has left the room.";
        
        if (g_rooms[oldRoom].empty()) {
            g_rooms.erase(oldRoom);
            std::cout << "[INFO] Room '" << oldRoom << "' is now empty and has been removed.\n";
        }
    }

    // 3. เข้าห้องใหม่
    g_rooms[sRoomName].push_back(clientHwnd);
    clientInfo.current_room = sRoomName;
    
    std::string joinMsg = "[SYSTEM] " + clientName + " has joined the room.";

    lock.unlock(); // --- ปลดล็อค ---
    
    std::cout << "[INFO] Client '" << clientName << "' joined room '" << sRoomName << "'\n";

    // 4. ส่งข้อความทั้งหมด
    if (!oldRoom.empty()) {
        BroadcastToRoom(oldRoom, RSP_SYSTEM_MSG, leaveMsg); // แจ้งห้องเก่า
    }
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, sRoomName);
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, "[SYSTEM] Welcome to " + sRoomName + "!");
    BroadcastToRoom(sRoomName, RSP_SYSTEM_MSG, joinMsg, clientHwnd); // แจ้งห้องใหม่
}

// (เพิ่มใหม่) "Write" Lock - Create-and-Join Logic
void HandleCreateRoom(HWND clientHwnd, const char* roomName) {
    std::unique_lock<std::shared_mutex> lock(g_registries_lock);

    std::string sRoomName(roomName);
    if (sRoomName.empty()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: Room name cannot be empty.");
        return;
    }

    // 1. ตรวจสอบว่าห้อง *ซ้ำ* หรือไม่
    if (g_rooms.find(sRoomName) != g_rooms.end()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: Room '" + sRoomName + "' already exists. Use /join to enter.");
        return;
    }

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string oldRoom = clientInfo.current_room;
    std::string clientName = clientInfo.name;
    std::string leaveMsg;

    // 2. ออกจากห้องเก่า (ถ้ามี)
    if (!oldRoom.empty() && g_rooms.find(oldRoom) != g_rooms.end()) {
        std::vector<HWND>& clientsInOldRoom = g_rooms[oldRoom];
        clientsInOldRoom.erase(std::remove(clientsInOldRoom.begin(), clientsInOldRoom.end(), clientHwnd), clientsInOldRoom.end());
        leaveMsg = "[SYSTEM] " + clientName + " has left the room.";
        
        if (g_rooms[oldRoom].empty()) {
            g_rooms.erase(oldRoom);
            std::cout << "[INFO] Room '" << oldRoom << "' is now empty and has been removed.\n";
        }
    }

    // 3. สร้างห้องใหม่ และเข้าห้อง
    g_rooms[sRoomName] = std::vector<HWND>(); // สร้างห้องว่าง
    g_rooms[sRoomName].push_back(clientHwnd); // เพิ่มตัวเองเข้าไป
    clientInfo.current_room = sRoomName;
    
    std::string createMsg = "[SYSTEM] Room '" + sRoomName + "' has been created.";
    
    lock.unlock(); // --- ปลดล็อค ---
    
    std::cout << "[INFO] Client '" << clientName << "' created and joined room '" << sRoomName << "'\n";

    // 4. ส่งข้อความ
    if (!oldRoom.empty()) {
        BroadcastToRoom(oldRoom, RSP_SYSTEM_MSG, leaveMsg); // แจ้งห้องเก่า
    }
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, sRoomName); // ยืนยันการเข้าห้อง
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, createMsg); // ส่งข้อความว่า "สร้างสำเร็จ"
}

// "Write" Lock
void HandleLeaveRoom(HWND clientHwnd) {
    std::unique_lock<std::shared_mutex> lock(g_registries_lock);

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string oldRoom = clientInfo.current_room;
    std::string clientName = clientInfo.name;

    if (oldRoom.empty()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are already in the lobby.");
        return;
    }

    // 1. ออกจากห้อง
    std::vector<HWND>& clientsInOldRoom = g_rooms[oldRoom];
    clientsInOldRoom.erase(std::remove(clientsInOldRoom.begin(), clientsInOldRoom.end(), clientHwnd), clientsInOldRoom.end());
    std::string leaveMsg = "[SYSTEM] " + clientName + " has left the room.";
    
    if (g_rooms[oldRoom].empty()) {
        g_rooms.erase(oldRoom);
        std::cout << "[INFO] Room '" << oldRoom << "' is now empty and has been removed.\n";
    }

    // 4. อัปเดตสถานะ client
    clientInfo.current_room = ""; // กลับไป Lobby
    
    lock.unlock(); // --- ปลดล็อค ---

    std::cout << "[INFO] Client '" << clientName << "' left room '" << oldRoom << "' and is now in Lobby.\n";
    
    // 5. ส่งข้อความ
    BroadcastToRoom(oldRoom, RSP_SYSTEM_MSG, leaveMsg);
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, ""); // ยืนยันว่ากลับมา Lobby
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, "[SYSTEM] You have returned to the Lobby.");
}

// "Write" Lock (เพราะต้องลบข้อมูล)
void HandleDisconnect(HWND clientHwnd) {
    std::unique_lock<std::shared_mutex> lock(g_registries_lock);
    
    if (g_clients.find(clientHwnd) == g_clients.end()) return; 

    ClientInfo clientInfo = g_clients[clientHwnd]; // Copy
    std::string name = clientInfo.name;
    std::string room = clientInfo.current_room;

    // 1. ลบจาก g_clients
    g_clients.erase(clientHwnd);
    std::string leaveMsg;

    // 2. ลบจาก g_rooms (ถ้ามี)
    if (!room.empty() && g_rooms.find(room) != g_rooms.end()) {
        std::vector<HWND>& clientsInRoom = g_rooms[room];
        clientsInRoom.erase(std::remove(clientsInRoom.begin(), clientsInRoom.end(), clientHwnd), clientsInRoom.end());
        leaveMsg = "[SYSTEM] " + name + " has left the room.";
        
        if (g_rooms[room].empty()) {
            g_rooms.erase(room);
            std::cout << "[INFO] Room '" << room << "' is now empty and has been removed.\n";
        }
    }

    lock.unlock(); // --- ปลดล็อค ---

    std::cout << "[INFO] Client '" << name << "' (" << clientHwnd << ") disconnected.\n";
    if (!leaveMsg.empty()) {
        BroadcastToRoom(room, RSP_SYSTEM_MSG, leaveMsg);
    }
}

// "Read" Lock (ใช้ shared_lock)
void HandleListRooms(HWND clientHwnd) {
    std::shared_lock<std::shared_mutex> lock(g_registries_lock);
    
    std::string roomList = "Available Rooms:\n";
    if (g_rooms.empty()) {
        roomList += "  (No rooms available)";
    } else {
        for (auto const& [name, clients] : g_rooms) {
            roomList += "  - " + name + " (" + std::to_string(clients.size()) + " user(s))\n";
        }
    }
    
    lock.unlock(); // --- ปลดล็อค ---
    
    SendToClient(clientHwnd, RSP_ROOM_LIST, roomList);
}

// "Read" Lock
void HandleChatMessage(HWND clientHwnd, const char* message) {
    std::shared_lock<std::shared_mutex> lock(g_registries_lock);

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string room = clientInfo.current_room;
    std::string name = clientInfo.name;

    if (room.empty()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are not in a room. Join a room first.");
        return;
    }

    std::string fullMsg = "[" + room + "] " + name + ": " + std::string(message);
    
    lock.unlock(); // --- ปลดล็อค ---
    
    std::cout << "[MSG] " << fullMsg << "\n";
    BroadcastToRoom(room, RSP_CHAT_MSG, fullMsg);
}

// "Read" Lock
void HandleWho(HWND clientHwnd) {
    std::shared_lock<std::shared_mutex> lock(g_registries_lock);

    std::string room = g_clients[clientHwnd].current_room;
    if (room.empty()) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_SYSTEM_MSG, "You are in the lobby."); // (แก้เป็น S_MSG)
        return;
    }

    std::string userList = "Users in room '" + room + "':\n";
    std::vector<HWND> clientsInRoom = g_rooms[room]; // Copy list

    for (HWND hwnd : clientsInRoom) {
        if (g_clients.count(hwnd)) {
            userList += "  - " + g_clients[hwnd].name + "\n";
        }
    }
    
    lock.unlock(); // --- ปลดล็อค ---
    
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, userList);
}

// "Read" Lock
void HandleDm(HWND clientHwnd, const char* payload) {
    std::string sPayload(payload);
    
    size_t first_space = sPayload.find(' ');
    if (first_space == std::string::npos) {
        SendToClient(clientHwnd, RSP_ERROR, "Error: Usage /dm <name> <message>");
        return;
    }

    std::string targetName = sPayload.substr(0, first_space);
    std::string dm_msg = sPayload.substr(first_space + 1);

    std::shared_lock<std::shared_mutex> lock(g_registries_lock);

    std::string senderName = g_clients[clientHwnd].name;

    if (targetName == senderName) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: You cannot DM yourself.");
        return;
    }

    HWND targetHwnd = NULL;
    for (auto const& [hwnd, info] : g_clients) {
        if (info.name == targetName) {
            targetHwnd = hwnd;
            break;
        }
    }

    if (targetHwnd == NULL) {
        lock.unlock();
        SendToClient(clientHwnd, RSP_ERROR, "Error: User '" + targetName + "' not found.");
        return;
    }

    std::string finalMsg = "[DM from " + senderName + "]: " + dm_msg;
    std::string confirmMsg = "[DM sent to " + targetName + "]";

    // (แก้ไข) เพิ่ม Log ใน Server
    std::cout << "[DM] " << senderName << " -> " << targetName << ": " << dm_msg << "\n";

    lock.unlock(); // --- ปลดล็อค ---

    SendToClient(targetHwnd, RSP_CHAT_MSG, finalMsg);
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, confirmMsg);
}

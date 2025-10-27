// Complie : g++ -std=c++17 chat_server.cpp -o chat_server.exe -luser32 -lgdi32
// ./chat_server.exe


#include <windows.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm> // For std::remove

// --- ชื่อหน้าต่างที่ Client จะใช้ค้นหา ---
const char* SERVER_CLASS = "ChatServerWindowClass";
const char* SERVER_TITLE = "Chat Server"; // ต้องตรงกับที่ Client ใช้

// --- Protocol (Client to Server) ---
#define CMD_REGISTER     100 // lParam: "ClientName"
#define CMD_JOIN_ROOM    101 // lParam: "RoomName"
#define CMD_CHAT_MSG     102 // lParam: "Hello world"
#define CMD_LIST_ROOMS   103 // lParam: NULL
#define CMD_DISCONNECT   104 // lParam: NULL
#define CMD_LEAVE_ROOM   105 // (เพิ่มใหม่) lParam: NULL

// --- Protocol (Server to Client) ---
#define RSP_CHAT_MSG     200 // lParam: "[Room] User: Hello"
#define RSP_SYSTEM_MSG   201 // lParam: "[SYSTEM] User has joined."
#define RSP_ROOM_LIST    202 // lParam: "Rooms:\n - main\n"
#define RSP_JOIN_SUCCESS 203 // lParam: "RoomName" (ถ้า "" = Lobby)
#define RSP_ERROR        204 // lParam: "Name already taken."

// --- Server State ---
struct ClientInfo {
    std::string name;
    std::string current_room;
};
std::map<HWND, ClientInfo> g_clients;
std::map<std::string, std::vector<HWND>> g_rooms;

// --- Prototypes ---
LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void HandleDisconnect(HWND clientHwnd);
void SendToClient(HWND clientHwnd, DWORD responseType, const std::string& message);
void BroadcastToRoom(const std::string& roomName, DWORD responseType, const std::string& message, HWND excludeHwnd);
void HandleLeaveRoom(HWND clientHwnd); // (เพิ่มใหม่)

// --- Main ---
int main() {
    // ... (โค้ดส่วน main เหมือนเดิมทุกประการ ไม่ต้องแก้) ...
    WNDCLASS wc = {};
    wc.lpfnWndProc = ServerWndProc;
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
    std::cout << "  Chat Server Started (MQ Version)\n";
    std::cout << "==================================\n";
    std::cout << "[INFO] Waiting for clients...\n";

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}


// --- Helpers ---

// ... (SendToClient และ BroadcastToRoom เหมือนเดิม) ...
void SendToClient(HWND clientHwnd, DWORD responseType, const std::string& message) {
    if (!IsWindow(clientHwnd)) {
        std::cerr << "[WARN] Trying to send to invalid window " << clientHwnd << ". Cleaning up.\n";
        HandleDisconnect(clientHwnd); // ทำความสะอาด
        return;
    }
    COPYDATASTRUCT cds;
    cds.dwData = responseType;
    cds.cbData = (DWORD)message.length() + 1;
    cds.lpData = (PVOID)message.c_str();
    SendMessageTimeout(clientHwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds, SMTO_NORMAL, 1000, NULL);
}

void BroadcastToRoom(const std::string& roomName, DWORD responseType, const std::string& message, HWND excludeHwnd = NULL) {
    if (g_rooms.find(roomName) == g_rooms.end()) return;
    std::vector<HWND> deadClients;
    std::vector<HWND>& clientsInRoom = g_rooms[roomName];
    for (HWND clientHwnd : clientsInRoom) {
        if (clientHwnd == excludeHwnd) continue;
        if (!IsWindow(clientHwnd)) {
            deadClients.push_back(clientHwnd);
            continue;
        }
        COPYDATASTRUCT cds;
        cds.dwData = responseType;
        cds.cbData = (DWORD)message.length() + 1;
        cds.lpData = (PVOID)message.c_str();
        SendMessageTimeout(clientHwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds, SMTO_NORMAL, 1000, NULL);
    }
    for (HWND deadClient : deadClients) {
        std::cerr << "[INFO] Found dead client " << deadClient << " in room " << roomName << ". Cleaning up.\n";
        HandleDisconnect(deadClient);
    }
}

// ... (HandleRegister, HandleJoinRoom, HandleChatMessage, HandleListRooms เหมือนเดิม) ...
void HandleRegister(HWND clientHwnd, const char* name) {
    std::string sName(name);
    for (auto const& [hwnd, info] : g_clients) {
        if (info.name == sName) {
            SendToClient(clientHwnd, RSP_ERROR, "Error: Name '" + sName + "' is already taken.");
            return;
        }
    }
    g_clients[clientHwnd] = { sName, "" };
    std::cout << "[INFO] Client registered: " << clientHwnd << " as '" << sName << "'\n";
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, ""); 
}

void HandleJoinRoom(HWND clientHwnd, const char* roomName) {
    if (g_clients.find(clientHwnd) == g_clients.end()) {
        SendToClient(clientHwnd, RSP_ERROR, "Error: Not registered. Please register first.");
        return;
    }
    std::string sRoomName(roomName);
    if (sRoomName.empty()) { // ป้องกันการสร้างห้องชื่อว่าง
        SendToClient(clientHwnd, RSP_ERROR, "Error: Room name cannot be empty.");
        return;
    }

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string oldRoom = clientInfo.current_room;

    if (oldRoom == sRoomName) {
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are already in this room.");
        return;
    }

    // 1. ออกจากห้องเก่า (ถ้ามี)
    if (!oldRoom.empty() && g_rooms.find(oldRoom) != g_rooms.end()) {
        std::vector<HWND>& clientsInOldRoom = g_rooms[oldRoom];
        clientsInOldRoom.erase(std::remove(clientsInOldRoom.begin(), clientsInOldRoom.end(), clientHwnd), clientsInOldRoom.end());
        std::string leaveMsg = "[SYSTEM] " + clientInfo.name + " has left the room.";
        BroadcastToRoom(oldRoom, RSP_SYSTEM_MSG, leaveMsg);
        if (g_rooms[oldRoom].empty()) {
            g_rooms.erase(oldRoom);
            std::cout << "[INFO] Room '" << oldRoom << "' is now empty and has been removed.\n";
        }
    }

    // 2. เข้าห้องใหม่
    g_rooms[sRoomName].push_back(clientHwnd);
    clientInfo.current_room = sRoomName;
    std::cout << "[INFO] Client '" << clientInfo.name << "' joined room '" << sRoomName << "'\n";

    // 3. ส่งข้อความต้อนรับ
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, sRoomName);
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, "[SYSTEM] Welcome to " + sRoomName + "!");

    // 4. ส่งข้อความแจ้งทุกคนในห้อง
    std::string joinMsg = "[SYSTEM] " + clientInfo.name + " has joined the room.";
    BroadcastToRoom(sRoomName, RSP_SYSTEM_MSG, joinMsg, clientHwnd);
}

void HandleChatMessage(HWND clientHwnd, const char* message) {
    if (g_clients.find(clientHwnd) == g_clients.end()) return;
    ClientInfo& clientInfo = g_clients[clientHwnd];
    if (clientInfo.current_room.empty()) {
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are not in a room. Join a room first.");
        return;
    }
    std::string fullMsg = "[" + clientInfo.current_room + "] " + clientInfo.name + ": " + std::string(message);
    std::cout << "[MSG] " << fullMsg << "\n";
    BroadcastToRoom(clientInfo.current_room, RSP_CHAT_MSG, fullMsg);
}

void HandleListRooms(HWND clientHwnd) {
    std::string roomList = "Available Rooms:\n";
    if (g_rooms.empty()) {
        roomList += "  (No rooms available)";
    } else {
        for (auto const& [name, clients] : g_rooms) {
            roomList += "  - " + name + " (" + std::to_string(clients.size()) + " user(s))\n";
        }
    }
    SendToClient(clientHwnd, RSP_ROOM_LIST, roomList);
}


// --- (เพิ่มใหม่) ---
// ฟังก์ชันสำหรับออกจากห้องกลับไป Lobby
void HandleLeaveRoom(HWND clientHwnd) {
    if (g_clients.find(clientHwnd) == g_clients.end()) return;

    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string oldRoom = clientInfo.current_room;

    if (oldRoom.empty()) {
        SendToClient(clientHwnd, RSP_ERROR, "Error: You are already in the lobby.");
        return;
    }

    // 1. ออกจากห้อง
    std::vector<HWND>& clientsInOldRoom = g_rooms[oldRoom];
    clientsInOldRoom.erase(std::remove(clientsInOldRoom.begin(), clientsInOldRoom.end(), clientHwnd), clientsInOldRoom.end());
    
    std::cout << "[INFO] Client '" << clientInfo.name << "' left room '" << oldRoom << "' and is now in Lobby.\n";
    
    // 2. แจ้งคนในห้องที่เหลือ
    std::string leaveMsg = "[SYSTEM] " + clientInfo.name + " has left the room.";
    BroadcastToRoom(oldRoom, RSP_SYSTEM_MSG, leaveMsg);

    // 3. ลบห้องถ้าว่าง
    if (g_rooms[oldRoom].empty()) {
        g_rooms.erase(oldRoom);
        std::cout << "[INFO] Room '" << oldRoom << "' is now empty and has been removed.\n";
    }

    // 4. อัปเดตสถานะ client และส่งการยืนยัน
    clientInfo.current_room = ""; // กลับไป Lobby
    SendToClient(clientHwnd, RSP_JOIN_SUCCESS, ""); // ส่ง "" หมายถึงการยืนยันว่ากลับมา Lobby
    SendToClient(clientHwnd, RSP_SYSTEM_MSG, "[SYSTEM] You have returned to the Lobby.");
}

// ... (HandleDisconnect เหมือนเดิม) ...
void HandleDisconnect(HWND clientHwnd) {
    if (g_clients.find(clientHwnd) == g_clients.end()) return; 
    ClientInfo& clientInfo = g_clients[clientHwnd];
    std::string name = clientInfo.name;
    std::string room = clientInfo.current_room;
    std::cout << "[INFO] Client '" << name << "' (" << clientHwnd << ") disconnected.\n";
    if (!room.empty() && g_rooms.find(room) != g_rooms.end()) {
        std::vector<HWND>& clientsInRoom = g_rooms[room];
        clientsInRoom.erase(std::remove(clientsInRoom.begin(), clientsInRoom.end(), clientHwnd), clientsInRoom.end());
        std::string leaveMsg = "[SYSTEM] " + name + " has left the room.";
        BroadcastToRoom(room, RSP_SYSTEM_MSG, leaveMsg);
        if (g_rooms[room].empty()) {
            g_rooms.erase(room);
            std::cout << "[INFO] Room '" << room << "' is now empty and has been removed.\n";
        }
    }
    g_clients.erase(clientHwnd);
}


// --- Window Procedure หลักของ Server ---
LRESULT CALLBACK ServerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COPYDATA: {
            HWND senderHwnd = (HWND)wParam;
            COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
            const char* payload = (const char*)cds->lpData;

            // ตรวจสอบว่า Client นี้ลงทะเบียนหรือยัง (ยกเว้นคำสั่ง Register)
            if (g_clients.find(senderHwnd) == g_clients.end() && cds->dwData != CMD_REGISTER) {
                std::cerr << "[WARN] Received command " << cds->dwData << " from unregistered client " << senderHwnd << ". Ignoring.\n";
                return 1;
            }

            switch (cds->dwData) {
                case CMD_REGISTER:
                    HandleRegister(senderHwnd, payload);
                    break;
                case CMD_JOIN_ROOM:
                    HandleJoinRoom(senderHwnd, payload);
                    break;
                case CMD_CHAT_MSG:
                    HandleChatMessage(senderHwnd, payload);
                    break;
                case CMD_LIST_ROOMS:
                    HandleListRooms(senderHwnd);
                    break;
                case CMD_DISCONNECT:
                    HandleDisconnect(senderHwnd);
                    break;
                // --- (เพิ่มใหม่) ---
                case CMD_LEAVE_ROOM:
                    HandleLeaveRoom(senderHwnd);
                    break;
                default:
                    std::cerr << "[WARN] Received unknown command: " << cds->dwData << "\n";
            }
            return 1; 
        }

        case WM_DESTROY:
            std::cout << "[INFO] Server shutting down...\n";
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

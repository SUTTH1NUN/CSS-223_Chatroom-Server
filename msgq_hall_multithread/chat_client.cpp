// Compile : g++ -std=c++17 chat_client.cpp -o chat_client.exe -luser32 -lgdi32 -static

#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

// --- ชื่อหน้าต่าง Server ที่จะค้นหา ---
const char* SERVER_CLASS = "ChatServerWindowClass";
const char* SERVER_TITLE = "Chat Server"; 

// --- Protocol (Client to Server) ---
#define CMD_REGISTER     100
#define CMD_JOIN_ROOM    101
#define CMD_CHAT_MSG     102
#define CMD_LIST_ROOMS   103
#define CMD_DISCONNECT   104
#define CMD_LEAVE_ROOM   105
#define CMD_WHO          106 
#define CMD_DM           107 
#define CMD_CREATE_ROOM  108 // (เพิ่มใหม่)

// --- Protocol (Server to Client) ---
#define RSP_CHAT_MSG     200
#define RSP_SYSTEM_MSG   201
#define RSP_ROOM_LIST    202
#define RSP_JOIN_SUCCESS 203 // (ถ้า "" = Lobby)
#define RSP_ERROR        204

// --- Client State --- 
const char* CLIENT_CLASS = "ChatClientWindowClass";
HWND g_serverHwnd = NULL;
HWND g_clientHwnd = NULL;
std::atomic<bool> g_running(true);
std::string g_myName;
std::string g_currentRoom; // ถ้าว่าง ("") = อยู่ Lobby

// --- Prototypes ---
LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void MessagePumpThread();
bool SendCommand(DWORD commandType, const std::string& payload);
void ShowPrompt();

// ------------------------
// Window Procedure (รับข้อความจาก Server)
// ------------------------
LRESULT CALLBACK ClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COPYDATA: {
            COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
            std::string message = (char*)cds->lpData;
            
            switch (cds->dwData) {
                case RSP_CHAT_MSG:
                case RSP_SYSTEM_MSG:
                case RSP_ROOM_LIST:
                case RSP_ERROR:
                    std::cout << "\n" << message << std::endl;
                    ShowPrompt(); // <-- พิมพ์ Prompt ใหม่
                    break;
                
                case RSP_JOIN_SUCCESS:
                    g_currentRoom = message; // <-- อัปเดตสถานะห้อง!
                    if (message.empty()) {
                        // กลับมา Lobby
                        std::cout << "\n[CLIENT] Returned to Lobby." << std::endl;
                        ShowPrompt(); // <-- พิมพ์ Prompt ใหม่
                    } else {
                        // เข้าห้องสำเร็จ
                        std::cout << "\n[CLIENT] Successfully joined room '" << message << "'.\n";
                        // (ไม่ต้องพิมพ์ Prompt ที่นี่ รอข้อความ "Welcome" หรือ "Created")
                    }
                    break;
            }
            
            return 1;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ... (MessagePumpThread และ SendCommand เหมือนเดิม) ...
void MessagePumpThread() {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = ClientWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLIENT_CLASS;
    if (!RegisterClassExA(&wc)) {
        std::cerr << "Failed to register window class" << std::endl;
        g_running = false;
        return;
    }
    g_clientHwnd = CreateWindowExA(
        0, CLIENT_CLASS, "ChatClient", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL
    );
    if (!g_clientHwnd) {
        std::cerr << "Failed to create client window" << std::endl;
        g_running = false;
        return;
    }
    MSG msg;
    while (g_running && GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool SendCommand(DWORD commandType, const std::string& payload = "") {
    if (!g_serverHwnd || !g_clientHwnd) return false;
    COPYDATASTRUCT cds;
    cds.dwData = commandType;
    cds.cbData = (DWORD)payload.length() + 1; // +1 สำหรับ null terminator
    cds.lpData = (PVOID)payload.c_str();
    if (SendMessage(g_serverHwnd, WM_COPYDATA, (WPARAM)g_clientHwnd, (LPARAM)&cds) == 0) {
        std::cerr << "\n[ERROR] Failed to send message to server. Server might be down.\n";
        g_running = false;
        return false;
    }
    return true;
}

void ShowPrompt() {
    if (g_currentRoom.empty()) {
        std::cout << "[Lobby] > ";
    } else {
        std::cout << "[" << g_currentRoom << "] > ";
    }
    std::cout.flush();
}

// ------------------------
// MAIN
// ------------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    g_serverHwnd = FindWindowA(SERVER_CLASS, SERVER_TITLE);
    if (!g_serverHwnd) {
        std::cerr << "[ERROR] ไม่พบ server! กรุณาเปิด server ก่อน" << std::endl;
        return 1;
    }
    
    std::thread pumpThread(MessagePumpThread);

    while (g_clientHwnd == NULL && g_running) {
        Sleep(50);
    }
    if (!g_running) {
        pumpThread.join();
        return 1;
    }
    
    std::cout << "Enter your name: ";
    std::getline(std::cin, g_myName);
    
    if (!SendCommand(CMD_REGISTER, g_myName)) {
        g_running = false;
        pumpThread.join();
        return 1;
    }

    // --- (อัปเดตข้อความช่วยเหลือ) ---
    std::cout << "\n==================================" << std::endl;
    std::cout << "  Welcome to the Lobby, " << g_myName << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << " Commands:" << std::endl;
    std::cout << "  /list               - Show all Rooms" << std::endl;
    std::cout << "  /create <room_name> - Create a new room" << std::endl;
    std::cout << "  /join <room_name>   - Join Room" << std::endl;
    std::cout << "  /leave              - Return to Lobby" << std::endl;
    std::cout << "  /who                - Show users in current room" << std::endl;
    std::cout << "  /dm <name> <msg>    - Send Direct Message" << std::endl;
    std::cout << "  /quit               - Quit Server" << std::endl;
    std::cout << "==================================" << std::endl << std::endl;
    
    
    std::string input;
    while (g_running) {
        ShowPrompt(); 
        std::getline(std::cin, input);
        
        if (!g_running) break;
        if (input.empty()) continue;
        
        if (input == "/quit") {
            SendCommand(CMD_DISCONNECT);
            g_running = false;
            break;
        }
        else if (input == "/list") {
            SendCommand(CMD_LIST_ROOMS);
        }
        else if (input.rfind("/join ", 0) == 0) { 
            std::string roomName = input.substr(6); 
            if (roomName.empty()) {
                std::cout << "[ERROR] Usage: /join <room_name>\n";
            } else {
                SendCommand(CMD_JOIN_ROOM, roomName);
            }
        }
        // --- (เพิ่มใหม่) ---
        else if (input.rfind("/create ", 0) == 0) {
            std::string roomName = input.substr(8);
            if (roomName.empty()) {
                std::cout << "[ERROR] Usage: /create <room_name>\n";
            } else {
                SendCommand(CMD_CREATE_ROOM, roomName);
            }
        }
        // --- (จบส่วนเพิ่ม) ---
        else if (input == "/leave") {
            if (g_currentRoom.empty()) {
                std::cout << "[ERROR] You are already in the lobby.\n";
            } else {
                SendCommand(CMD_LEAVE_ROOM);
            }
        }
        else if (input == "/who") {
            if (g_currentRoom.empty()) {
                std::cout << "[ERROR] You are in the lobby. Use /who in a room.\n";
            } else {
                SendCommand(CMD_WHO);
            }
        }
        else if (input.rfind("/dm ", 0) == 0) {
            std::string payload = input.substr(4);
            if (payload.find(' ') == std::string::npos || payload.length() < 3) {
                std::cout << "[ERROR] Usage: /dm <name> <message>\n";
            } else {
                SendCommand(CMD_DM, payload);
            }
        }
        else {
            if (g_currentRoom.empty()) {
                std::cout << "[ERROR] You are in the lobby. Use /create or /join to chat.\n";
            } else {
                SendCommand(CMD_CHAT_MSG, input);
            }
        }
    }
    
    // ทำความสะอาด
    PostMessage(g_clientHwnd, WM_QUIT, 0, 0);
    if (pumpThread.joinable()) {
        pumpThread.join();
    }
    
    DestroyWindow(g_clientHwnd);
    
    std::cout << "\n[CLIENT] Disconnected" << std::endl;
    return 0;
}

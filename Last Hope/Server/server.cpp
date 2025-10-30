// chat_client_mq.cpp
// Client for Chat over MSMQ
// Build: g++ -std=c++17 chat_client_mq.cpp -o chat_client_mq.exe -lole32 -luuid -lmqrt

#include <windows.h>
#include <mq.h>
#pragma comment(lib, "mqrt.lib")

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <random>
#include <algorithm>

// ===== Protocol (must match server) =====
#define CMD_REGISTER     100
#define CMD_JOIN_ROOM    101
#define CMD_CHAT_MSG     102
#define CMD_LIST_ROOMS   103
#define CMD_DISCONNECT   104
#define CMD_LEAVE_ROOM   105
#define CMD_WHO          106
#define CMD_DM           107
#define CMD_CREATE_ROOM  108

#define RSP_CHAT_MSG     200
#define RSP_SYSTEM_MSG   201
#define RSP_ROOM_LIST    202
#define RSP_JOIN_SUCCESS 203
#define RSP_ERROR        204

// ===== MSMQ queue path =====
static const wchar_t* SERVER_QUEUE_PATH = L".\\private$\\chat_server";

// ===== Globals =====
std::atomic<bool> g_running{true};
std::string g_myName;
std::string g_currentRoom;
std::wstring g_myReplyQueuePath; // e.g. L".\\private$\\chat_c_<pid>_<rand>"

// ===== Helpers =====
void ShowPrompt() {
    if (g_currentRoom.empty()) std::cout << "[Lobby] > ";
    else std::cout << "[" << g_currentRoom << "] > ";
    std::cout.flush();
}

HRESULT EnsurePrivateQueue(const std::wstring& path) {
    // If exists and openable, return S_OK
    WCHAR fmt[256]; DWORD len = 256;
    HRESULT hr = MQPathNameToFormatName(const_cast<LPWSTR>(path.c_str()), fmt, &len);
    if (SUCCEEDED(hr)) {
        QUEUEHANDLE h = nullptr;
        hr = MQOpenQueue(fmt, MQ_RECEIVE_ACCESS, MQ_DENY_NONE, &h);
        if (SUCCEEDED(hr)) { MQCloseQueue(h); return S_OK; }
    }
    // Create queue
    PROPID aPropId[2]; MQPROPVARIANT aPropVar[2];
    aPropId[0] = PROPID_Q_PATHNAME; aPropVar[0].vt = VT_LPWSTR; aPropVar[0].pwszVal = const_cast<LPWSTR>(path.c_str());
    aPropId[1] = PROPID_Q_LABEL;    aPropVar[1].vt = VT_LPWSTR; aPropVar[1].pwszVal = const_cast<LPWSTR>(L"chat_client");
    MQQUEUEPROPS props{}; props.cProp = 2; props.aPropID = aPropId; props.aPropVar = aPropVar; props.aStatus = nullptr;
    WCHAR fmtOut[256]; DWORD fmtOutLen = 256;
    hr = MQCreateQueue(nullptr, &props, fmtOut, &fmtOutLen);
    if (hr == MQ_ERROR_QUEUE_EXISTS) return S_OK;
    return hr;
}

bool OpenQueueSend(const wchar_t* path, QUEUEHANDLE& hSend) {
    WCHAR fmt[256]; DWORD len = 256;
    if (FAILED(MQPathNameToFormatName(const_cast<LPWSTR>(path), fmt, &len))) return false;
    return SUCCEEDED(MQOpenQueue(fmt, MQ_SEND_ACCESS, MQ_DENY_NONE, &hSend));
}

bool OpenQueueReceive(const std::wstring& path, QUEUEHANDLE& hRecv) {
    WCHAR fmt[256]; DWORD len = 256;
    if (FAILED(MQPathNameToFormatName(const_cast<LPWSTR>(path.c_str()), fmt, &len))) return false;
    return SUCCEEDED(MQOpenQueue(fmt, MQ_RECEIVE_ACCESS, MQ_DENY_NONE, &hRecv));
}

// body = "cmd|<myReplyQueue>|payload"
bool SendCommandToServer(unsigned long cmd, const std::wstring& myReplyQ, const std::string& payload) {
    QUEUEHANDLE hq = nullptr;
    if (!OpenQueueSend(SERVER_QUEUE_PATH, hq)) {
        std::cerr << "[ERROR] Cannot open server queue for send\n";
        return false;
    }
    std::string rq(myReplyQ.begin(), myReplyQ.end());
    std::string body = std::to_string(cmd) + "|" + rq + "|" + payload;

    MQMSGPROPS props{}; PROPID id[3]; MQPROPVARIANT var[3]; HRESULT st[3]; int i=0;
    id[i] = PROPID_M_LABEL;     var[i].vt = VT_LPWSTR; var[i].pwszVal = const_cast<LPWSTR>(L"chat_cmd"); i++;
    id[i] = PROPID_M_BODY;      var[i].vt = VT_VECTOR | VT_UI1; var[i].caub.pElems = (UCHAR*)body.data(); var[i].caub.cElems = (ULONG)body.size(); i++;
    id[i] = PROPID_M_BODY_TYPE; var[i].vt = VT_UI4; var[i].ulVal = 8; i++;

    props.cProp = i; props.aPropID = id; props.aPropVar = var; props.aStatus = st;

    HRESULT hr = MQSendMessage(hq, &props, nullptr);
    MQCloseQueue(hq);
    if (FAILED(hr)) {
        std::cerr << "[ERROR] MQSendMessage failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }
    return true;
}

// Expect body = "rspType|text"
bool ReceiveResponse(QUEUEHANDLE hRecv, unsigned long& rspType, std::string& text) {
    const ULONG BUF = 8192;
    UCHAR body[BUF]{};
    PROPID id[3]; MQPROPVARIANT var[3]; HRESULT st[3];

    id[0] = PROPID_M_LABEL;     var[0].vt = VT_LPWSTR; var[0].pwszVal = new WCHAR[64]; var[0].pwszVal[0] = L'\0';
    id[1] = PROPID_M_BODY;      var[1].vt = VT_VECTOR | VT_UI1; var[1].caub.pElems = body; var[1].caub.cElems = BUF;
    id[2] = PROPID_M_BODY_TYPE; var[2].vt = VT_UI4;

    MQMSGPROPS props{}; props.cProp = 3; props.aPropID = id; props.aPropVar = var; props.aStatus = st;

    HRESULT hr = MQReceiveMessage(hRecv, INFINITE, MQ_ACTION_RECEIVE, &props, nullptr, nullptr, nullptr, MQ_NO_TRANSACTION);
    if (FAILED(hr)) {
        delete[] var[0].pwszVal;
        return false;
    }

    std::string b((char*)var[1].caub.pElems, var[1].caub.cElems);
    // Trim trailing '\0' padding
    auto it0 = std::find(b.begin(), b.end(), '\0');
    if (it0 != b.end()) b.erase(it0, b.end());

    size_t p = b.find('|');
    if (p == std::string::npos) {
        delete[] var[0].pwszVal;
        return false;
    }
    rspType = std::stoul(b.substr(0, p));
    text = b.substr(p + 1);

    delete[] var[0].pwszVal;
    return true;
}

// ===== Ctrl+C graceful shutdown =====
BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 1) สร้างคิวตอบกลับของ client
    std::mt19937_64 rng{ std::random_device{}() };
    unsigned long long rnd = rng();
    g_myReplyQueuePath = L".\\private$\\chat_c_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(rnd & 0xFFFF);
    if (FAILED(EnsurePrivateQueue(g_myReplyQueuePath))) {
        std::cerr << "[ERROR] Cannot create/open my reply queue\n";
        return 1;
    }

    // 2) เปิดคิวรับของตัวเอง
    QUEUEHANDLE hRecv = nullptr;
    if (!OpenQueueReceive(g_myReplyQueuePath, hRecv)) {
        std::cerr << "[ERROR] Cannot open my reply queue for receive\n";
        return 1;
    }

    // 3) เธรดรับข้อความจากเซิร์ฟเวอร์
    std::thread recvThread([&]{
        while (g_running) {
            unsigned long rsp; std::string msg;
            if (!ReceiveResponse(hRecv, rsp, msg)) {
                if (!g_running) break; // likely closing
                continue;
            }
            switch (rsp) {
                case RSP_CHAT_MSG:
                case RSP_SYSTEM_MSG:
                case RSP_ROOM_LIST:
                case RSP_ERROR:
                    std::cout << "\n" << msg << "\n";
                    ShowPrompt();
                    break;
                case RSP_JOIN_SUCCESS:
                    g_currentRoom = msg; // "" => Lobby
                    if (msg.empty()) {
                        std::cout << "\n[CLIENT] Returned to Lobby.\n";
                        ShowPrompt();
                    } else {
                        std::cout << "\n[CLIENT] Successfully joined room '" << msg << "'.\n";
                        // Wait for welcome/system message from server
                    }
                    break;
                default:
                    // ignore unknown types
                    break;
            }
        }
    });

    // 4) ลงทะเบียนชื่อกับเซิร์ฟเวอร์
    std::cout << "Enter your name: ";
    std::getline(std::cin, g_myName);
    if (g_myName.empty()) g_myName = "guest";

    if (!SendCommandToServer(CMD_REGISTER, g_myReplyQueuePath, g_myName)) {
        std::cerr << "[ERROR] Cannot contact server (register)\n";
        g_running = false;
        MQCloseQueue(hRecv);
        if (recvThread.joinable()) recvThread.join();
        return 1;
    }

    // 5) UI คำสั่ง
    std::cout << "\n==================================\n";
    std::cout << "  Welcome to the Lobby, " << g_myName << "\n";
    std::cout << "==================================\n";
    std::cout << " Commands:\n";
    std::cout << "  /list               - Show all Rooms\n";
    std::cout << "  /create <room>      - Create a new room\n";
    std::cout << "  /join <room>        - Join Room\n";
    std::cout << "  /leave              - Return to Lobby\n";
    std::cout << "  /who                - Show users in current room\n";
    std::cout << "  /dm <name> <msg>    - Send Direct Message\n";
    std::cout << "  /quit               - Quit\n";
    std::cout << "==================================\n\n";

    std::string input;
    while (g_running) {
        ShowPrompt();
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (input == "/quit") {
            SendCommandToServer(CMD_DISCONNECT, g_myReplyQueuePath, "");
            g_running = false;
            break;
        } else if (input == "/list") {
            SendCommandToServer(CMD_LIST_ROOMS, g_myReplyQueuePath, "");
        } else if (input.rfind("/join ", 0) == 0) {
            std::string room = input.substr(6);
            if (room.empty()) std::cout << "[ERROR] Usage: /join <room>\n";
            else SendCommandToServer(CMD_JOIN_ROOM, g_myReplyQueuePath, room);
        } else if (input.rfind("/create ", 0) == 0) {
            std::string room = input.substr(8);
            if (room.empty()) std::cout << "[ERROR] Usage: /create <room>\n";
            else SendCommandToServer(CMD_CREATE_ROOM, g_myReplyQueuePath, room);
        } else if (input == "/leave") {
            if (g_currentRoom.empty()) std::cout << "[ERROR] You are already in the lobby.\n";
            else SendCommandToServer(CMD_LEAVE_ROOM, g_myReplyQueuePath, "");
        } else if (input == "/who") {
            if (g_currentRoom.empty()) std::cout << "[ERROR] You are in the lobby. Use /who in a room.\n";
            else SendCommandToServer(CMD_WHO, g_myReplyQueuePath, "");
        } else if (input.rfind("/dm ", 0) == 0) {
            std::string payload = input.substr(4);
            if (payload.find(' ') == std::string::npos || payload.length() < 3) {
                std::cout << "[ERROR] Usage: /dm <name> <message>\n";
            } else {
                SendCommandToServer(CMD_DM, g_myReplyQueuePath, payload);
            }
        } else {
            if (g_currentRoom.empty()) {
                std::cout << "[ERROR] You are in the lobby. Use /create or /join to chat.\n";
            } else {
                SendCommandToServer(CMD_CHAT_MSG, g_myReplyQueuePath, input);
            }
        }
    }

    // 6) Cleanup
    g_running = false;
    // ปิดคิวรับเพื่อปลุก recvThread ที่กำลัง block
    MQCloseQueue(hRecv);
    if (recvThread.joinable()) recvThread.join();

    std::wcout << L"\n[CLIENT] Disconnected. (reply queue: " << g_myReplyQueuePath << L")\n";
    return 0;
}

#include <iostream>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include "../Shared/message.h"
using namespace std;

// =============================
//   GLOBAL STATE
// =============================
map<string, vector<string>> rooms;   // room -> members
map<string, string> userRoom;        // user -> room
map<string, bool> activeUsers;       // username -> logged in?

// =============================
//   COLOR + TIMESTAMP LOGGING
// =============================
#define GREEN "\033[1;32m"
#define CYAN  "\033[1;36m"
#define YELLOW "\033[1;33m"
#define MAGENTA "\033[1;35m"
#define RED "\033[1;31m"
#define RESET "\033[0m"

string timestamp() {
    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    tm *lt = localtime(&t);
    stringstream ss;
    ss << put_time(lt, "[%H:%M:%S] ");
    return ss.str();
}

void logLogin(const string &msg)  { cout << timestamp() << GREEN << "[LOGIN] " << RESET << msg << endl; }
void logInfo(const string &msg)   { cout << timestamp() << CYAN  << "[INFO]  " << RESET << msg << endl; }
void logWarn(const string &msg)   { cout << timestamp() << YELLOW << "[WARN]  " << RESET << msg << endl; }
void logDM(const string &msg)     { cout << timestamp() << MAGENTA << "[DM]    " << RESET << msg << endl; }
void logLeave(const string &msg)  { cout << timestamp() << RED << "[LEAVE] " << RESET << msg << endl; }

// =============================
//   SEND MESSAGE TO USER
// =============================
void sendTo(const string& user, const string& text) {
    string qname = "/reply_" + user;
    mqd_t mq = mq_open(qname.c_str(), O_WRONLY);
    if (mq == (mqd_t)-1) {
        logWarn("Cannot send to " + user + " (queue missing)");
        return;
    }

    Message msg{};
    strncpy(msg.sender, "Server", sizeof(msg.sender));
    strncpy(msg.text, text.c_str(), sizeof(msg.text));
    mq_send(mq, (char*)&msg, sizeof(msg), 0);
    mq_close(mq);
}

// =============================
//   BROADCAST TO ROOM
// =============================
void broadcast(const string& room, const string& from, const string& text) {
    if (!rooms.count(room)) return;
    for (auto& user : rooms[room]) {
        if (user != from)
            sendTo(user, "[" + room + "] " + from + ": " + text);
    }
    logInfo(from + " said in " + room + ": " + text);
}

// =============================
//   HANDLE COMMAND
// =============================
void handleMessage(const Message& msg) {
    string user = msg.sender;
    string text = msg.text;

    // --- First-time login detection ---
    if (!activeUsers[user]) {
        activeUsers[user] = true;
        logLogin(user + " connected.");
    }

    // === CREATE ROOM ===
    if (strncmp(text.c_str(), "/create ", 8) == 0) {
        string room = text.substr(8);
        if (!rooms.count(room)) {
            rooms[room] = {};
            logInfo(user + " created room: " + room);
        }
        rooms[room].push_back(user);
        userRoom[user] = room;
        sendTo(user, "Created and joined room: " + room);
    }

    // === LIST ROOMS ===
    else if (strncmp(text.c_str(), "/list", 5) == 0) {
        string out = "Rooms:\n";
        if (rooms.empty()) out += "(no rooms yet)";
        for (auto& [r, mem] : rooms)
            out += " - " + r + " (" + to_string(mem.size()) + " users)\n";
        sendTo(user, out);
        logInfo(user + " requested /list");
    }

    // === JOIN ROOM ===
    else if (strncmp(text.c_str(), "/join ", 6) == 0) {
        string room = text.substr(6);
        if (!rooms.count(room)) {
            sendTo(user, "Room not found: " + room);
            logWarn(user + " tried to join missing room " + room);
            return;
        }
        rooms[room].push_back(user);
        userRoom[user] = room;
        broadcast(room, "System", user + " joined the room.");
        sendTo(user, "Joined room: " + room);
        logInfo(user + " joined " + room);
    }

    // === SAY ===
    else if (strncmp(text.c_str(), "/say ", 5) == 0) {
        string msgText = text.substr(5);
        string room = userRoom[user];
        if (room.empty()) sendTo(user, "Join a room first!");
        else broadcast(room, user, msgText);
    }

    // === DM ===
    else if (strncmp(text.c_str(), "/dm ", 4) == 0) {
        size_t space = text.find(' ', 4);
        if (space == string::npos) return;
        string target = text.substr(4, space - 4);
        string msgText = text.substr(space + 1);
        sendTo(target, "[DM] " + user + ": " + msgText);
        sendTo(user, "[DM → " + target + "] " + msgText);
        logDM(user + " → " + target + ": " + msgText);
    }

    // === WHO ===
    else if (strncmp(text.c_str(), "/who", 4) == 0) {
        string room = userRoom[user];
        if (room.empty()) sendTo(user, "You are not in a room.");
        else {
            string out = "Members in " + room + ":\n";
            for (auto& u : rooms[room]) out += " - " + u + "\n";
            sendTo(user, out);
        }
        logInfo(user + " requested /who");
    }

    // === LEAVE ===
    else if (strncmp(text.c_str(), "/leave", 6) == 0) {
        string room = userRoom[user];
        if (room.empty()) sendTo(user, "You're not in any room.");
        else {
            auto& v = rooms[room];
            v.erase(remove(v.begin(), v.end(), user), v.end());
            userRoom[user].clear();
            broadcast(room, "System", user + " left the room.");
            sendTo(user, "Left room: " + room);
            logLeave(user + " left " + room);
        }
    }

    // === EXIT ===
    else if (strncmp(text.c_str(), "/exit", 5) == 0) {
        string room = userRoom[user];
        if (!room.empty()) {
            auto& v = rooms[room];
            v.erase(remove(v.begin(), v.end(), user), v.end());
            broadcast(room, "System", user + " disconnected.");
        }
        userRoom[user].clear();
        activeUsers[user] = false;
        sendTo(user, "Goodbye!");
        logLeave(user + " disconnected.");
    }

    // === UNKNOWN ===
    else {
        sendTo(user, "Unknown command: " + text);
        logWarn("Unknown command from " + user + ": " + text);
    }
}

// =============================
//   MAIN
// =============================
int main() {
    cout << GREEN << "[Server] Starting chat server..." << RESET << endl;

    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);
    attr.mq_curmsgs = 0;

    mq_unlink(CONTROL_QUEUE);
    mqd_t control_mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDONLY, 0644, &attr);
    if (control_mq == (mqd_t)-1) {
        perror("mq_open (server)");
        return 1;
    }

    cout << GREEN << "[Server] Control queue created ✅" << RESET << endl;
    cout << CYAN << "[Server] Waiting for clients..." << RESET << endl;

    Message msg{};
    while (true) {
        ssize_t bytes = mq_receive(control_mq, (char*)&msg, sizeof(msg), nullptr);
        if (bytes >= 0) handleMessage(msg);
    }

    mq_close(control_mq);
    mq_unlink(CONTROL_QUEUE);
    return 0;
}

#include "SenderThread.h"
#include <iostream>
#include <sstream>
#include <cstring>

SenderThread::SenderThread(mqd_t mq, const std::string& uname)
    : controlQueue(mq), username(uname) {}

void SenderThread::run() {
    std::string line;
    while (true) {
        std::getline(std::cin, line);
        if (line.empty()) continue;
        parseAndSend(line);
        if (line == "/exit") break;
    }
}

void SenderThread::parseAndSend(const std::string& cmd) {
    Message msg;
    strcpy(msg.sender, username.c_str());

    std::istringstream iss(cmd);
    std::string token;
    iss >> token;

    std::string temp;  // ใช้เก็บข้อความแบบ string ชั่วคราว

    if (token == "/create") {
        msg.type = CMD_CREATE;
        iss >> msg.target;
    }
    else if (token == "/list") msg.type = CMD_LIST;
    else if (token == "/join") {
        msg.type = CMD_JOIN;
        iss >> msg.target;
    }
    else if (token == "/exit") msg.type = CMD_EXIT;
    else if (token == "/say") {
        msg.type = CMD_SAY;
        std::getline(iss, temp);
        if (!temp.empty() && temp[0] == ' ') temp.erase(0, 1);
        strncpy(msg.text, temp.c_str(), sizeof(msg.text) - 1);
    }
    else if (token == "/dm") {
        msg.type = CMD_DM;
        iss >> msg.target;
        std::getline(iss, temp);
        if (!temp.empty() && temp[0] == ' ') temp.erase(0, 1);
        strncpy(msg.text, temp.c_str(), sizeof(msg.text) - 1);
    }
    else if (token == "/who") msg.type = CMD_WHO;
    else if (token == "/leave") msg.type = CMD_LEAVE;
    else msg.type = CMD_UNKNOWN;

    mq_send(controlQueue, (const char*)&msg, sizeof(Message), 0);
}


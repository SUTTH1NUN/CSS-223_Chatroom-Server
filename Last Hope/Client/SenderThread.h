#pragma once
#include <mqueue.h>
#include <string>
#include "../shared/message.h"

class SenderThread {
private:
    mqd_t controlQueue;
    std::string username;

public:
    SenderThread(mqd_t controlQueue, const std::string& username);
    void run();

private:
    void parseAndSend(const std::string& cmd);
};

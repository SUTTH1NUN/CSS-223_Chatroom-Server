#pragma once
#include <mqueue.h>

class ReceiverThread {
private:
    mqd_t replyQueue;

public:
    explicit ReceiverThread(mqd_t replyQueue);
    void run();
};

#pragma once
#include <string>
#include <mqueue.h>
#include <pthread.h>
#include "SenderThread.h"
#include "ReceiverThread.h"
#include "../shared/message.h"

class ClientApp {
private:
    std::string username;
    std::string replyQueueName;
    mqd_t controlQueue;
    mqd_t replyQueue;
    pthread_t senderThread;
    pthread_t receiverThread;

public:
    ClientApp(const std::string& username);
    ~ClientApp();

    void start();
    void stop();

private:
    static void* senderThreadFunc(void* arg);
    static void* receiverThreadFunc(void* arg);
};

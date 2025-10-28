#include "ClientApp.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

ClientApp::ClientApp(const std::string& uname) : username(uname) {
    replyQueueName = "/reply_" + username;

    // เปิด control queue (ของ server)
    controlQueue = mq_open("/control_queue", O_WRONLY);
    if (controlQueue == -1) {
        perror("mq_open(control_queue)");
        exit(1);
    }

    // สร้าง reply queue สำหรับ client นี้
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);
    replyQueue = mq_open(replyQueueName.c_str(), O_CREAT | O_RDONLY, 0666, &attr);
    if (replyQueue == -1) {
        perror("mq_open(replyQueue)");
        exit(1);
    }
}

ClientApp::~ClientApp() {
    mq_close(controlQueue);
    mq_close(replyQueue);
    mq_unlink(replyQueueName.c_str());
}

void ClientApp::start() {
    std::cout << "Welcome, " << username << "! Type commands below:\n";

    pthread_create(&senderThread, nullptr, senderThreadFunc, this);
    pthread_create(&receiverThread, nullptr, receiverThreadFunc, this);

    pthread_join(senderThread, nullptr);
    pthread_cancel(receiverThread);
}

void* ClientApp::senderThreadFunc(void* arg) {
    auto* app = static_cast<ClientApp*>(arg);
    SenderThread sender(app->controlQueue, app->username);
    sender.run();
    return nullptr;
}

void* ClientApp::receiverThreadFunc(void* arg) {
    auto* app = static_cast<ClientApp*>(arg);
    ReceiverThread receiver(app->replyQueue);
    receiver.run();
    return nullptr;
}

void ClientApp::stop() {
    mq_close(controlQueue);
    mq_close(replyQueue);
}

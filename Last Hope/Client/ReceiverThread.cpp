#include "ReceiverThread.h"
#include "../shared/message.h"
#include <iostream>
#include <unistd.h>

ReceiverThread::ReceiverThread(mqd_t rq) : replyQueue(rq) {}

void ReceiverThread::run() {
    Message msg;
    while (true) {
        ssize_t bytes = mq_receive(replyQueue, (char*)&msg, sizeof(Message), nullptr);
        if (bytes > 0) {
            std::cout << "[" << msg.sender << "]: " << msg.text << std::endl;
        } else {
            perror("mq_receive");
            sleep(1);
        }
    }
}

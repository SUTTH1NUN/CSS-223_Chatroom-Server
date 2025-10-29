#ifndef RECEIVER_THREAD_H
#define RECEIVER_THREAD_H

#include <mqueue.h>

class ReceiverThread {
private:
    mqd_t mq;
public:
    ReceiverThread(mqd_t queue) : mq(queue) {}
    void* run(); 
};

#endif

#include "ReceiverThread.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

void* ReceiverThread::run()
{
    const size_t BUF_SIZE = 1024;
    char buffer[BUF_SIZE];
    unsigned int prio;

    while (true)
    {
        ssize_t bytes = mq_receive(mq, buffer, BUF_SIZE, &prio);
        if (bytes >= 0)
        {
            buffer[bytes] = '\0';
            std::cout << buffer << std::endl;
        }
        else
        {
            perror("mq_receive");
            sleep(1);
        }
    }
    return nullptr;
}

#include <iostream>
#include <mqueue.h>
#include <cstring>
#include <thread>
#include "message.h"

const char* CONTROL_QUEUE = "/control_queue";
const size_t MAX_SIZE = 1024;

// Router thread function
void router_thread() {
    mqd_t mq;
    char buffer[MAX_SIZE];

    // เปิด control queue สำหรับอ่าน
    mq = mq_open(CONTROL_QUEUE, O_RDONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open (router)");
        exit(1);
    }

    std::cout << "[Router] Started, waiting for messages...\n";

    while (true) {
        memset(buffer, 0, MAX_SIZE);
        ssize_t bytes = mq_receive(mq, buffer, MAX_SIZE, nullptr);
        if (bytes >= 0) {
            std::string msg(buffer);
            std::cout << "[Router] Received: " << msg << std::endl;
        } else {
            perror("mq_receive");
        }
    }

    mq_close(mq);
}

int main() {
    // ตั้งค่า attribute ของ message queue
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;       // queue size
    attr.mq_msgsize = MAX_SIZE;
    attr.mq_curmsgs = 0;

    // ลบ queue เก่าถ้ามี
    mq_unlink(CONTROL_QUEUE);

    // สร้าง control queue (read-write)
    mqd_t mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open (server)");
        exit(1);
    }
    mq_close(mq);

    std::cout << "[Server] Control queue created: " << CONTROL_QUEUE << std::endl;

    // สร้าง router thread
    std::thread router(router_thread);
    router.join();

    // ปิด queue ตอนจบ
    mq_unlink(CONTROL_QUEUE);
    return 0;
}

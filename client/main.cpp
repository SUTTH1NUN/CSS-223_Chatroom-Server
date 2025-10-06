#include <iostream>
#include <mqueue.h>
#include <cstring>
#include <thread>
#include <unistd.h>
#include "message.h"

const char* CONTROL_QUEUE = "/control_queue";
const size_t MAX_SIZE = 1024;

// ฟัง reply queue ของตัวเอง
void listen_reply(const std::string& reply_queue_name) {
    mqd_t mq;
    char buffer[MAX_SIZE];

    mq = mq_open(reply_queue_name.c_str(), O_RDONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open (reply)");
        exit(1);
    }

    std::cout << "[Client] Listening on " << reply_queue_name << std::endl;

    while (true) {
        memset(buffer, 0, MAX_SIZE);
        ssize_t bytes = mq_receive(mq, buffer, MAX_SIZE, nullptr);
        if (bytes >= 0) {
            std::string msg(buffer);
            std::cout << "[Client] Received: " << msg << std::endl;
        } else {
            perror("mq_receive");
        }
    }

    mq_close(mq);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./client <username>\n";
        return 1;
    }

    std::string username = argv[1];
    std::string reply_queue_name = "/reply_" + username;

    // สร้าง reply queue ของตัวเอง
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_SIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(reply_queue_name.c_str()); // ลบของเก่า
    mqd_t reply_mq = mq_open(reply_queue_name.c_str(),
                             O_CREAT | O_RDWR, 0644, &attr);
    if (reply_mq == (mqd_t)-1) {
        perror("mq_open (client reply)");
        return 1;
    }
    mq_close(reply_mq);

    // ส่ง JOIN message ไปที่ control queue
    mqd_t control_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (control_mq == (mqd_t)-1) {
        perror("mq_open (control)");
        return 1;
    }

    std::string join_msg = "JOIN|" + username + "|os-lab|";
    mq_send(control_mq, join_msg.c_str(), join_msg.size() + 1, 0);
    mq_close(control_mq);

    std::cout << "[Client] " << username << " joined room os-lab\n";

    // สร้าง thread คอยฟัง reply queue
    std::thread listener(listen_reply, reply_queue_name);
    listener.join();

    mq_unlink(reply_queue_name.c_str());
    return 0;
}

#include <iostream>
#include <mqueue.h>
#include <cstring>
#include <thread>
#include <unistd.h>

const char* CONTROL_QUEUE = "/control_queue";
const size_t MAX_SIZE = 1024;

void listen_reply(const std::string& reply_queue_name) {
    mqd_t mq;
    char buffer[MAX_SIZE];
    mq = mq_open(reply_queue_name.c_str(), O_RDONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open (reply)");
        exit(1);
    }

    while (true) {
        memset(buffer, 0, MAX_SIZE);
        ssize_t bytes = mq_receive(mq, buffer, MAX_SIZE, nullptr);
        if (bytes >= 0) {
            std::string msg(buffer);
            std::cout << "[recv] " << msg << std::endl;
        }
    }
    mq_close(mq);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: ./client <username> <room>\n";
        return 1;
    }

    std::string username = argv[1];
    std::string room = argv[2];
    std::string reply_queue_name = "/reply_" + username;

    // create reply queue
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_SIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(reply_queue_name.c_str());
    mqd_t reply_mq = mq_open(reply_queue_name.c_str(),
                             O_CREAT | O_RDWR, 0644, &attr);
    mq_close(reply_mq);

    // send JOIN
    mqd_t control_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    std::string join_msg = "JOIN|" + username + "|" + room + "|";
    mq_send(control_mq, join_msg.c_str(), join_msg.size() + 1, 0);
    mq_close(control_mq);

    std::cout << "[sent] " << username << " joined " << room << std::endl;

    std::thread listener(listen_reply, reply_queue_name);

    // loop: read input from stdin and send SAY
    while (true) {
        std::string line;
        std::getline(std::cin, line);
        if (line == "/quit") break;

        mqd_t ctrl = mq_open(CONTROL_QUEUE, O_WRONLY);

        std::string msg;
        if (line.rfind("/dm ", 0) == 0) {
            // format: /dm <user> <text>
            size_t sp = line.find(' ', 4);
            if (sp != std::string::npos) {
                std::string target = line.substr(4, sp-4);
                std::string text = line.substr(sp+1);
                msg = "DM|" + username + "|" + target + "|" + text;
            } else {
                std::cout << "[sent] Usage: /dm <user> <text>\n";
                mq_close(ctrl);
                continue;
            }
        } else {
            msg = "SAY|" + username + "|" + room + "|" + line;
        }

        mq_send(ctrl, msg.c_str(), msg.size() + 1, 0);
        mq_close(ctrl);
    }

    listener.join();
    mq_unlink(reply_queue_name.c_str());
    return 0;
}
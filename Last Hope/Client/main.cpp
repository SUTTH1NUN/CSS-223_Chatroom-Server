#include <iostream>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include "../Shared/message.h"

using namespace std;

void receiverThread(string username) {
    string replyQueue = "/reply_" + username;
    mqd_t reply_mq = mq_open(replyQueue.c_str(), O_RDONLY);
    if (reply_mq == (mqd_t)-1) {
        perror("mq_open (reply)");
        return;
    }

    Message msg{};
    while (true) {
        ssize_t bytes = mq_receive(reply_mq, (char*)&msg, sizeof(msg), nullptr);
        if (bytes > 0) {
            cout << "\n" << msg.text << "\n> ";
            fflush(stdout);
        }
    }
}

int main() {
    string username;
    cout << "Enter your username: ";
    cin >> username;

    // สร้าง reply queue
    string replyQueue = "/reply_" + username;
    mq_unlink(replyQueue.c_str());

    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);
    attr.mq_curmsgs = 0;
    mq_open(replyQueue.c_str(), O_CREAT | O_RDONLY, 0644, &attr);

    mqd_t control_mq = mq_open(CONTROL_QUEUE, O_WRONLY);
    if (control_mq == (mqd_t)-1) {
        perror("mq_open (control)");
        return 1;
    }

    thread t(receiverThread, username);
    t.detach();

    cout << "====================\n"
         << "/list              - list all rooms\n"
         << "/create <room>     - create a new room\n"
         << "/join <room>       - join existing room\n"
         << "/say <msg>         - chat in current room\n"
         << "/dm <user> <msg>   - send private message\n"
         << "/who               - list users in room\n"
         << "/leave             - leave current room\n"
         << "/exit              - disconnect\n"
         << "====================\n";

    while (true) {
        cout << "What you want to do?";
        cout << "> ";
        string line;
        getline(cin >> ws, line);

        Message msg{};
        strncpy(msg.sender, username.c_str(), sizeof(msg.sender));
        strncpy(msg.text, line.c_str(), sizeof(msg.text));

        mq_send(control_mq, (char*)&msg, sizeof(msg), 0);

        if (line == "/exit") break;
    }

    mq_close(control_mq);
    mq_unlink(replyQueue.c_str());
    return 0;
}

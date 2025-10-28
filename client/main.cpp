// client.cpp
#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
using namespace std;

const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;

int main() {
    cout << "Enter your username: ";
    string username;
    getline(cin, username);

    // ตรวจสอบว่า server เปิดอยู่หรือไม่
    mqd_t control_mq = mq_open(CONTROL_QUEUE, O_WRONLY | O_NONBLOCK);
    if (control_mq == (mqd_t)-1) {
        if (errno == ENOENT) {
            cerr << "[Error] Server is not running (queue not found).\n";
        } else if (errno == ENXIO) {
            cerr << "[Error] Server queue exists but no process is listening.\n";
        } else {
            perror("mq_open control");
        }
        exit(1);
    }


    // สร้าง reply queue ของ client
    string reply_q = "/reply_" + to_string(getpid());
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(reply_q.c_str());
    mqd_t reply_mq = mq_open(reply_q.c_str(), O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (reply_mq == (mqd_t)-1) {
        perror("mq_open reply");
        return 1;
    }

    cout << "Connected to server ✅\n";
    cout << "Commands:\n"
         << "  /create <room>\n"
         << "  /list\n"
         << "  /join <room>\n"
         << "  /exit\n";

    string line;
    char buf[MQ_MSGSIZE];

    while (true) {
        cout << "> ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        string cmd;
        stringstream ss(line);
        ss >> cmd;

        string payload;

        if (cmd == "/create") {
            string room; ss >> room;
            if (room.empty()) {
                cout << "Usage: /create <room>\n";
                continue;
            }
            payload = "CREATE|" + reply_q + "|" + room;
        }
        else if (cmd == "/list") {
            payload = "LIST|" + reply_q;
        }
        else if (cmd == "/join") {
            string room; ss >> room;
            if (room.empty()) {
                cout << "Usage: /join <room>\n";
                continue;
            }
            payload = "JOIN|" + reply_q + "|" + room + "|" + username;
        }
        else if (cmd == "/exit") {
            payload = "EXIT|" + reply_q;
            mq_send(control_mq, payload.c_str(), payload.size() + 1, 0);
            cout << "Exiting...\n";
            break;
        }
        else {
            cout << "Unknown command.\n";
            continue;
        }

        mq_send(control_mq, payload.c_str(), payload.size() + 1, 0);

        // รอรับ response
        while (true) {
            ssize_t bytes = mq_receive(reply_mq, buf, MQ_MSGSIZE, nullptr);
            if (bytes >= 0) {
                cout << string(buf) << endl;
                break;
            } else {
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        }
    }

    mq_close(control_mq);
    mq_close(reply_mq);
    mq_unlink(reply_q.c_str());
    return 0;
}

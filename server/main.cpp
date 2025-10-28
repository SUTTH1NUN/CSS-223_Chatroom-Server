// server.cpp
#include <bits/stdc++.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace std;

const char* CONTROL_QUEUE = "/chat_control";
const long MQ_MSGSIZE = 1024;
mqd_t mq;

struct Room {
    string name;
    vector<string> members;
};

void handle_sigint(int) {
    cout << "\n[Server] Caught SIGINT, cleaning up..." << endl;
    mq_close(mq);
    mq_unlink(CONTROL_QUEUE);
    cout << "[Server] Queue removed. Exiting.\n";
    exit(0);
}

int main() {
    signal(SIGINT, handle_sigint); 

    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSGSIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(CONTROL_QUEUE);
    mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open server");
        return 1;
    }

    cout << "[Server] Started. Waiting for clients...\n";

    map<string, Room> rooms; // room name -> Room struct
    char buf[MQ_MSGSIZE];

    while (true) {
        ssize_t bytes = mq_receive(mq, buf, MQ_MSGSIZE, nullptr);
        if (bytes < 0) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        string msg(buf);
        vector<string> parts;
        string token;
        stringstream ss(msg);
        while (getline(ss, token, '|')) parts.push_back(token);
        if (parts.empty()) continue;

        string cmd = parts[0];
        string reply_q;
        if (parts.size() >= 2) reply_q = parts[1];

        auto send_reply = [&](const string& text) {
            mqd_t client_q = mq_open(reply_q.c_str(), O_WRONLY);
            if (client_q != (mqd_t)-1) {
                mq_send(client_q, text.c_str(), text.size() + 1, 0);
                mq_close(client_q);
            }
        };

        if (cmd == "CREATE" && parts.size() >= 3) {
            string room_name = parts[2];
            if (rooms.count(room_name)) {
                send_reply("SYSTEM|Room already exists: " + room_name);
            } else {
                rooms[room_name] = Room{room_name, {}};
                send_reply("SYSTEM|Room created: " + room_name);
                cout << "[Server] Created room: " << room_name << endl;
            }
        } 
        else if (cmd == "LIST") {
            string result = "LIST|";
            for (auto &r : rooms) {
                result += r.first + " ";
            }
            send_reply(result);
        }
        else if (cmd == "JOIN" && parts.size() >= 4) {
            string room_name = parts[2];
            string username = parts[3];
            if (!rooms.count(room_name)) {
                send_reply("SYSTEM|Room does not exist: " + room_name);
            } else {
                auto &members = rooms[room_name].members;
                if (find(members.begin(), members.end(), username) == members.end()) {
                    members.push_back(username);
                    send_reply("SYSTEM|Joined room: " + room_name);
                    cout << "[Server] " << username << " joined " << room_name << endl;
                } else {
                    send_reply("SYSTEM|You are already in room: " + room_name);
                }
            }
        }
        else if (cmd == "EXIT") {
            send_reply("SYSTEM|Goodbye!");
            cout << "[Server] A client disconnected.\n";
        }
        else {
            send_reply("SYSTEM|Unknown command.");
        }
    }

    mq_close(mq);
    mq_unlink(CONTROL_QUEUE);
    return 0;
}

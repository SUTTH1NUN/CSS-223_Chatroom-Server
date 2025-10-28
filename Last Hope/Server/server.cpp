#include "../shared/message.h"
#include <mqueue.h>
#include <map>
#include <vector>
#include <iostream>
using namespace std;

map<string, vector<string>> roomMembers;  // room -> list of usernames
map<string, string> clientRoom;           // client -> room

void handleMessage(const Message& msg) {
    switch (msg.type) {
        case CMD_CREATE:
            if (!roomMembers.count(msg.target)) {
                roomMembers[msg.target] = {};
                cout << "Room created: " << msg.target << endl;
            }
            break;

        case CMD_LIST:
            cout << "[Server] List rooms requested by " << msg.sender << endl;
            for (auto& [name, members] : roomMembers)
                cout << " - " << name << " (" << members.size() << " users)" << endl;
            break;

        case CMD_JOIN:
            roomMembers[msg.target].push_back(msg.sender);
            clientRoom[msg.sender] = msg.target;
            cout << msg.sender << " joined room " << msg.target << endl;
            break;

        case CMD_SAY:
            cout << "[" << clientRoom[msg.sender] << "] "
                 << msg.sender << ": " << msg.text << endl;
            break;

        case CMD_DM:
            cout << "(DM) " << msg.sender << " → " << msg.target << ": "
                 << msg.text << endl;
            break;

        case CMD_WHO:
            cout << "[Server] Listing members for " << msg.sender << endl;
            break;

        case CMD_LEAVE:
            roomMembers[clientRoom[msg.sender]].erase(
                remove(roomMembers[clientRoom[msg.sender]].begin(),
                       roomMembers[clientRoom[msg.sender]].end(),
                       msg.sender),
                roomMembers[clientRoom[msg.sender]].end()
            );
            clientRoom[msg.sender] = "";
            cout << msg.sender << " left the room.\n";
            break;

        case CMD_EXIT:
            cout << msg.sender << " disconnected.\n";
            break;

        default:
            cout << "[Error] Unknown command.\n";
    }
}

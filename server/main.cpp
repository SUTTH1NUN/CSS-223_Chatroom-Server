#include <iostream>
#include <mqueue.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "../message.h"

const char* CONTROL_QUEUE = "/control_queue";
const size_t MAX_SIZE = 1024;

// Room Registry: room -> set of client usernames
std::unordered_map<std::string, std::unordered_set<std::string>> roomRegistry;
// Client Registry: username -> reply queue name
std::unordered_map<std::string, std::string> clientRegistry;
std::mutex registry_mutex;

// helper: send message to specific queue
bool send_to_queue(const std::string& queue_name, const std::string& payload) {
    mqd_t mq = mq_open(queue_name.c_str(), O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open (send)");
        return false;
    }
    mq_send(mq, payload.c_str(), payload.size() + 1, 0);
    mq_close(mq);
    return true;
}

// broadcast to all clients in a room
void broadcast(const std::string& room, const std::string& sender, const std::string& text) {
    std::lock_guard<std::mutex> lk(registry_mutex);

    auto roomIt = roomRegistry.find(room);
    if (roomIt == roomRegistry.end()) {
        std::cerr << "[Server] Room not found: " << room << std::endl;
        return;
    }

    for (const auto& user : roomIt->second) {
        //skip own message
        if (user == sender) continue;

        auto clientIt = clientRegistry.find(user);
        if (clientIt != clientRegistry.end()) {
            std::string msg = "SAY|" + sender + "|" + room + "|" + text;
            send_to_queue(clientIt->second, msg);
        }
    }

    std::cout << "[Server] Broadcast in room " << room
              << " from " << sender << ": " << text << std::endl;
}

// send direct message
void direct_message(const std::string& sender, const std::string& target, const std::string& text) {
    std::lock_guard<std::mutex> lk(registry_mutex);
    auto it = clientRegistry.find(target);
    if (it == clientRegistry.end()) {
        std::cerr << "[Server] DM failed: target not found -> " << target << std::endl;
        // แจ้งกลับ sender ว่าส่งไม่ได้
        auto its = clientRegistry.find(sender);
        if (its != clientRegistry.end()) {
            send_to_queue(its->second, "SYSTEM|server||User " + target + " not found");
        }
        return;
    }

    std::string msg = "DM|" + sender + "|" + target + "|" + text;
    send_to_queue(it->second, msg);
    std::cout << "[Server] Direct message " << sender << " -> " << target << ": " << text << std::endl;
}

// router thread
void router_thread() {
    mqd_t mq;
    char buffer[MAX_SIZE];
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

            // parse message: TYPE|sender|target|text
            size_t p1 = msg.find('|');
            size_t p2 = msg.find('|', p1+1);
            size_t p3 = msg.find('|', p2+1);

            std::string type   = msg.substr(0, p1);
            std::string sender = msg.substr(p1+1, p2-p1-1);
            std::string target = msg.substr(p2+1, p3-p2-1);
            std::string text   = msg.substr(p3+1);

            if (type == "JOIN") {
                // add to room
                std::lock_guard<std::mutex> lk(registry_mutex);
                roomRegistry[target].insert(sender);
                clientRegistry[sender] = "/reply_" + sender;
                std::cout << "[Server] " << sender << " joined " << target << std::endl;
            }
            else if (type == "SAY") {
                broadcast(target, sender, text);
            }else if (type == "DM"){
                direct_message(sender, target, text);
            }

        } else {
            perror("mq_receive");
        }
    }
    mq_close(mq);
}

int main() {
    // set queue attr
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MAX_SIZE;
    attr.mq_curmsgs = 0;

    mq_unlink(CONTROL_QUEUE);
    mqd_t mq = mq_open(CONTROL_QUEUE, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open (server)");
        exit(1);
    }
    mq_close(mq);

    std::cout << "[Server] Control queue created: " << CONTROL_QUEUE << std::endl;

    std::thread router(router_thread);
    router.join();

    mq_unlink(CONTROL_QUEUE);
    return 0;
}

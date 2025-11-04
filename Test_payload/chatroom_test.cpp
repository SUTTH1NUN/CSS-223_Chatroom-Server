#include <iostream>
#include <thread>
#include <arpa/inet.h>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <string>
#include <cstring>
#include <fstream>
#include <chrono>
#include <atomic>
#include <netinet/in.h>
#include <unistd.h>
using namespace std;

const int PORT = 5555;
const int BUFFER_SIZE = 1024;
atomic<bool> running(true);

mutex queue_mutex;
condition_variable queue_cv;
queue<int> client_queue;

// ======= SERVER ========
void handle_client(int client_socket, int id) {
    char buffer[BUFFER_SIZE];
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            close(client_socket);
            break;
        }
        cout << "[Server Thread " << id << "] Received: " << buffer << endl;

        // Echo back to simulate response
        send(client_socket, "ACK", 3, 0);
    }
}

void server_main(int server_threads) {
    int server_fd, new_socket;
    struct sockaddr_in address{};
    int opt = 1;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 50);

    cout << "[Server] Listening on port " << PORT << " with " << server_threads << " threads.\n";

    vector<thread> workers;
    for (int i = 0; i < server_threads; i++) {
        workers.emplace_back([i]() {
            while (running) {
                int client_socket;
                {
                    unique_lock<mutex> lock(queue_mutex);
                    queue_cv.wait(lock, [] { return !client_queue.empty() || !running; });
                    if (!running) break;
                    client_socket = client_queue.front();
                    client_queue.pop();
                }
                handle_client(client_socket, i);
            }
        });
    }

    while (running) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) break;
        {
            lock_guard<mutex> lock(queue_mutex);
            client_queue.push(new_socket);
        }
        queue_cv.notify_one();
    }

    running = false;
    queue_cv.notify_all();
    for (auto &t : workers) t.join();
    close(server_fd);
}

// ======= CLIENT ========
vector<string> read_messages_from_file(const string &filename) {
    ifstream file(filename);
    vector<string> lines;
    string line;
    if (!file.is_open()) {
        ofstream create(filename);
        create << "Hello from test client\nThis is a performance test\nHow are you today?\nLast line!\n";
        create.close();
        cout << "[Client] File 'messages.txt' not found. Created sample file.\n";
        file.open(filename);
    }
    while (getline(file, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    file.close();
    return lines;
}

void client_thread_func(int id, const vector<string> &messages,
                        vector<double> &latencies, mutex &lat_mtx) {
    int sock = 0;
    struct sockaddr_in serv_addr{};
    char buffer[BUFFER_SIZE];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "[Client " << id << "] Connection Failed\n";
        return;
    }

    for (auto &msg : messages) {
        auto start = chrono::high_resolution_clock::now();
        send(sock, msg.c_str(), msg.size(), 0);
        recv(sock, buffer, BUFFER_SIZE, 0); // Wait for ACK
        auto end = chrono::high_resolution_clock::now();

        chrono::duration<double, milli> latency = end - start;
        {
            lock_guard<mutex> lock(lat_mtx);
            latencies.push_back(latency.count());
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    close(sock);
}

void client_main(int client_count, int thread_per_client) {
    vector<string> messages = read_messages_from_file("messages.txt");

    cout << "[Client] Starting " << client_count << " clients (" 
         << thread_per_client << " threads per client)\n";

    vector<thread> clients;
    vector<double> latencies;
    mutex lat_mtx;

    auto global_start = chrono::high_resolution_clock::now();

    for (int i = 0; i < client_count * thread_per_client; i++) {
        clients.emplace_back(client_thread_func, i, cref(messages), ref(latencies), ref(lat_mtx));
        this_thread::sleep_for(chrono::milliseconds(50)); // stagger connection
    }

    for (auto &c : clients) c.join();
    auto global_end = chrono::high_resolution_clock::now();

    chrono::duration<double> total_time = global_end - global_start;

    // Calculate latency stats
    double sum = 0, min_lat = 1e9, max_lat = 0;
    for (double l : latencies) {
        sum += l;
        min_lat = min(min_lat, l);
        max_lat = max(max_lat, l);
    }
    double avg = latencies.empty() ? 0 : sum / latencies.size();

    cout << "\n===== PERFORMANCE RESULT =====\n";
    cout << "Messages sent: " << latencies.size() << endl;
    cout << "Total time: " << total_time.count() << " sec\n";
    cout << "Average latency: " << avg << " ms\n";
    cout << "Min latency: " << min_lat << " ms\n";
    cout << "Max latency: " << max_lat << " ms\n";
    cout << "================================\n";
}

// ======= MAIN ========
int main() {
    cout << "Enter command (/test): ";
    string cmd;
    std::getline(cin, cmd);

    if (cmd != "/test") {
        cout << "Unknown command.\n";
        return 0;
    }

    cout << "Run as (1) Server or (2) Client? ";
    int mode;
    cin >> mode;

    if (mode == 1) {
        int server_threads;
        cout << "Enter number of server threads: ";
        cin >> server_threads;
        server_main(server_threads);
    } else if (mode == 2) {
        int client_count, thread_per_client;
        cout << "Enter number of clients: ";
        cin >> client_count;
        cout << "Enter threads per client: ";
        cin >> thread_per_client;
        client_main(client_count, thread_per_client);
    } else {
        cout << "Invalid mode.\n";
    }

    return 0;
}
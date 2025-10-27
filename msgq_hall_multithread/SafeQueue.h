#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <windows.h> // For HWND, DWORD

// "ใบออเดอร์" ที่ Router จะส่งให้ Worker
struct Job {
    HWND        senderHwnd;    // Client ที่ส่งคำสั่ง
    DWORD       commandType;   // ประเภทคำสั่ง (CMD_...)
    std::string payload;       // ข้อมูล (เช่น "room_A" หรือ "hello")
};

// คิวที่ปลอดภัย (Thread-Safe) สำหรับเก็บ Job
class SafeQueue {
public:
    // เพิ่มงาน (Router เรียก)
    void push(Job job) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(std::move(job));
        lock.unlock();
        m_cv.notify_one(); // ปลุก Worker ที่หลับอยู่ 1 ตัว
    }

    // ดึงงาน (Worker เรียก)
    Job pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        // รอจนกว่าคิวจะมีงาน
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        
        Job job = std::move(m_queue.front());
        m_queue.pop();
        return job;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

private:
    std::queue<Job>       m_queue;
    std::mutex            m_mutex;
    std::condition_variable m_cv;
};

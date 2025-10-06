#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>

// ประเภทข้อความ (protocol)
enum class MessageType {
    JOIN,       // JOIN room
    SAY,        // SAY room text
    DM,         // Direct Message
    WHO,        // WHO room
    LEAVE,      // LEAVE room
    QUIT,       // QUIT system
    SYSTEM,     // System notice เช่น "Alice joined"
    LIST        // ใช้ส่งรายชื่อกลับไป
};

// โครงสร้างข้อความที่ client และ server ใช้ส่งหากัน
struct Message {
    MessageType type;   // ประเภทข้อความ
    std::string sender; // ใครเป็นคนส่ง
    std::string target; // ห้องหรือ user ปลายทาง
    std::string text;   // เนื้อหาข้อความ
};

#endif // MESSAGE_H

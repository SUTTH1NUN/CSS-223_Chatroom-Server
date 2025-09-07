// #ifdef MESSAGE_H
// #define MESSAGE_H
// #include <string>

// enum class MessageType{
//     JOIN,
//     SAY,
//     DM,
//     WHO,
//     LEAVE,
//     QUIT,
//     SYSTEM,
//     LIST
// };

// struct message
// {
//     MessageType type;
//     std::string sender;
//     std::string target;
//     std::string text;
// };

// #endif


#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>

// ประเภทของข้อความ
enum class MessageType {
    JOIN,
    SAY,
    DM,
    WHO,
    LEAVE,
    QUIT,
    SYSTEM,
    LIST
};

// โครงสร้างข้อความ
struct Message {
    MessageType type;   // ประเภทข้อความ
    std::string sender; // ใครเป็นคนส่ง
    std::string target; // ห้องหรือ user ปลายทาง
    std::string text;   // เนื้อหาข้อความ
};

#endif // MESSAGE_H


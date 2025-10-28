#ifndef MESSAGE_H
#define MESSAGE_H

#define CONTROL_QUEUE "/control_queue"

enum CommandType {
    CMD_CREATE,
    CMD_LIST,
    CMD_JOIN,
    CMD_SAY,
    CMD_DM,
    CMD_WHO,
    CMD_LEAVE,
    CMD_EXIT
};

struct Message {
    char sender[32];
    char target[32];
    char text[256];
    int type;
};

#endif

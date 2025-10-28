// shared/message.h
#pragma once
#include <cstring>

enum CommandType {
    CMD_CREATE, CMD_LIST, CMD_JOIN, CMD_EXIT,
    CMD_SAY, CMD_DM, CMD_WHO, CMD_LEAVE,
    CMD_UNKNOWN
};

struct Message {
    CommandType type;
    char sender[32];
    char target[32];     // room name or username
    char text[256];
};

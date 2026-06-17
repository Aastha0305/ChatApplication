#pragma once
#include <string>

struct Message {
    std::string sender;
    std::string recipient; // empty for room messages
    std::string room; // empty for private messages
    std::string body;
    bool isPrivate = false;
    bool delivered = false;
};

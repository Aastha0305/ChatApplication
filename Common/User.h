#pragma once
#include <string>

struct User {
    std::string username;
    std::string password;
    std::string lastSeen; // ISO timestamp
};

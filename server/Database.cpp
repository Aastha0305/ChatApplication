#include "Database.h"
#include <iostream>

// TODO: Move SQLite helper functions and db variable from server/main.cpp into this file.

bool OpenDatabase() {
    std::cout << "Database::OpenDatabase scaffold" << std::endl;
    return true;
}

void CloseDatabase() {
    std::cout << "Database::CloseDatabase scaffold" << std::endl;
}

bool InitDatabase() { return true; }

bool LoadUsers() { return true; }
bool SaveUser(const std::string &username, const std::string &password) { return true; }

bool InsertMessage(const std::string &sender, const std::string &recipient, const std::string &room, const std::string &body, bool isPrivate, bool delivered) { return true; }
bool DeliverOfflineMessages(const std::string &username, int clientSocket) { return true; }

bool UpdateLastSeen(const std::string &username) { return true; }
bool GetLastSeen(const std::string &username, std::string &response) { return true; }

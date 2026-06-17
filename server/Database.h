#pragma once

#include <string>

// Database API (scaffold)

bool OpenDatabase();
void CloseDatabase();
bool InitDatabase();

// User persistence
bool LoadUsers();
bool SaveUser(const std::string &username, const std::string &password);

// Messages
bool InsertMessage(const std::string &sender, const std::string &recipient, const std::string &room, const std::string &body, bool isPrivate, bool delivered);
bool DeliverOfflineMessages(const std::string &username, int clientSocket);

// Metadata
bool UpdateLastSeen(const std::string &username);
bool GetLastSeen(const std::string &username, std::string &response);

#pragma once

#include <WinSock2.h>
#include <string>

// Room management API (scaffold)

bool CreateRoom(SOCKET clientSocket, const std::string &roomName, std::string &response);
bool JoinRoom(SOCKET clientSocket, const std::string &roomName, std::string &response);
bool LeaveRoom(SOCKET clientSocket, const std::string &roomName, std::string &response);
bool RoomExists(const std::string &roomName);

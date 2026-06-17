#include "RoomManager.h"
#include <iostream>

// TODO: Move room-related functions and `rooms` / `clientRoom` data structures from server/main.cpp here.

bool CreateRoom(SOCKET clientSocket, const std::string &roomName, std::string &response) {
    std::cout << "RoomManager::CreateRoom scaffold" << std::endl;
    response = "Room scaffold: " + roomName + "\n";
    return true;
}

bool JoinRoom(SOCKET clientSocket, const std::string &roomName, std::string &response) {
    std::cout << "RoomManager::JoinRoom scaffold" << std::endl;
    response = "Joined scaffold: " + roomName + "\n";
    return true;
}

bool LeaveRoom(SOCKET clientSocket, const std::string &roomName, std::string &response) {
    std::cout << "RoomManager::LeaveRoom scaffold" << std::endl;
    response = "Left scaffold: " + roomName + "\n";
    return true;
}

bool RoomExists(const std::string &roomName) {
    return false;
}

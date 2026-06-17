#pragma once

#include <WinSock2.h>
#include <string>

// Client management API (scaffold)

bool IsAuthenticated(SOCKET clientSocket);
std::string GetAuthenticatedUsername(SOCKET clientSocket);
std::string GetClientRoom(SOCKET clientSocket);
void RemoveClient(SOCKET clientSocket);
void BroadcastMessage(const std::string &message, SOCKET senderSocket, const std::string &roomName);
void InteractWithClient(SOCKET clientSocket);

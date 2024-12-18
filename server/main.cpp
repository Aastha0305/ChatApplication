// Server Code
#define _WIN32_WINNT 0x0600
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <thread>
#include <vector>

#include <fstream> // For file logging
#pragma comment(lib, "ws2_32.lib")
using namespace std;

/*
initialise winsock library
create the socket
get ip and port
bind the ip/port with the socket
listen on the socket
accept
receive and send
close the socket
clean the winsock
*/

vector<SOCKET> clients;
ofstream chatLog("chat_history.txt", ios::app); // Log file for chat messages

// Function to handle communication with a client
void InteractWithClient(SOCKET clientSocket,vector<SOCKET> &clients) {
    cout << "Client connected" << endl;
    chatLog << "Client connected\n"; // Log connection
    char buffer[4096];

    while (true) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            cout << "Client disconnected" << endl;
            chatLog << "Client disconnected\n"; // Log disconnection
            break;
        }

        string message(buffer, bytesReceived);
        cout << "Message from client: " << message << endl;
        chatLog << "Message from client: " << message << endl; // Log the message

        // Broadcast the message to other clients
        for (auto client : clients) {
            if (client != clientSocket) {
                send(client, message.c_str(), message.length(), 0);
            }
        }
    }

    // Remove the client from the vector
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (*it == clientSocket) {
            clients.erase(it);
            break;
        }
    }
    closesocket(clientSocket);
}

bool Initialize() {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

int main() {
    if (!Initialize()) {
        cerr << "Winsock initialization failed" << endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    inet_pton(AF_INET, "0.0.0.0", &serverAddr.sin_addr);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server listening on port 12345" << endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        clients.push_back(clientSocket);
        thread clientThread(InteractWithClient, clientSocket);
        clientThread.detach();
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}

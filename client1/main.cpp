// Client Code
#define _WIN32_WINNT 0x0600
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <thread>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

void SendMessageToServer(SOCKET s) {
    cout << "Type /register <username> <password> or /login <username> <password> to join the chat." << endl;
    cout << "Then create/join a room with /create <room-name> or /join <room-name>." << endl;
    cout << "Type /leave <room-name> to leave a room and /quit to exit.\n";

    string message;
    while (true) {
        if (!getline(cin, message)) {
            break;
        }

        if (message.empty()) {
            continue;
        }

        int bytesSent = send(s, message.c_str(), static_cast<int>(message.length()), 0);
        if (bytesSent == SOCKET_ERROR) {
            cerr << "Error sending message: " << WSAGetLastError() << endl;
            break;
        }

        if (message == "/quit") {
            cout << "You exited the chat. Press Ctrl+C to terminate the client." << endl;
            break;
        }
    }
}

void ReceiveMessagesFromServer(SOCKET s) {
    char buffer[4096];
    while (true) {
        int bytesReceived = recv(s, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            cerr << "Disconnected from server" << endl;
            break;
        }

        string receivedMessage(buffer, bytesReceived);
        cout << receivedMessage;
    }
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

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(s, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Failed to connect to server: " << WSAGetLastError() << endl;
        closesocket(s);
        WSACleanup();
        return 1;
    }

    cout << "Connected to server" << endl;

    thread sendThread(SendMessageToServer, s);
    thread receiveThread(ReceiveMessagesFromServer, s);

    sendThread.detach();
    receiveThread.join();

    closesocket(s);
    WSACleanup();
    return 0;
}



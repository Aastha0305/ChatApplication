// Client Code
#define _WIN32_WINNT 0x0600
#include <iostream>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <thread>
#include <fstream> // For file logging
#pragma comment(lib, "ws2_32.lib")
using namespace std;

ofstream clientLog("client_history.txt", ios::app); // Log file for client messages
/*
initialise winsock library
create the socket
connect to the server
send/rcv
close the socket
clean the winsock
*/
// Function to send messages
void SendMessageToServer(SOCKET s) {
    cout << "Enter your chat name: ";
    string name;
    getline(cin, name);

    string message;
    while (true) {
        getline(cin, message);
        string fullMessage = name + ": " + message;
        clientLog << fullMessage << endl; // Log sent message

        int bytesSent = send(s, fullMessage.c_str(), fullMessage.length(), 0);
        if (bytesSent == SOCKET_ERROR) {
            cerr << "Error sending message: " << WSAGetLastError() << endl;
            break;
        }

        if (message == "quit") {
            cout << "You exited the chat. Press Ctrl+C to terminate the client." << endl;
            break;
        }

    }
}

// Function to receive messages
void ReceiveMessagesFromServer(SOCKET s) {
    char buffer[4096];
    while (true) {
        int bytesReceived = recv(s, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            cerr << "Disconnected from server" << endl;
            break;
        }

        string receivedMessage(buffer, bytesReceived);
        cout << receivedMessage << endl;
        clientLog << "Received: " << receivedMessage << endl; // Log received message
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



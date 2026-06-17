// Server Code
#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <queue>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <sqlite3.h>
using namespace std;

// POSIX socket compatibility aliases for previous Windows code
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

// Forward declarations (placed early so WorkerLoop can call InteractWithClient)
void InteractWithClient(SOCKET clientSocket);
bool SendToClient(SOCKET clientSocket, const string &message);

vector<SOCKET> clients;
unordered_map<SOCKET, string> authenticatedUsers;
unordered_map<string, string> storedUsers;
unordered_map<string, unordered_set<SOCKET>> rooms;
unordered_map<SOCKET, string> clientRoom;
mutex clientsMutex;
mutex usersMutex;
mutex dbMutex;
sqlite3 *db = nullptr;
queue<SOCKET> taskQueue;
mutex taskMutex;
condition_variable taskAvailable;
bool shutdownPool = false;
vector<thread> workerThreads;
// Rate limiting: per-client counters
mutex rateMutex;
struct RateState { int count = 0; chrono::steady_clock::time_point windowStart = chrono::steady_clock::now(); };
unordered_map<SOCKET, RateState> rateStates;

vector<string> SplitString(const string &text) {
    vector<string> tokens;
    istringstream stream(text);
    string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

string Trim(const string &text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    size_t end = text.find_last_not_of(" \t\r\n");
    return (start == string::npos) ? string() : text.substr(start, end - start + 1);
}

string FormatTimestamp(const string &timestamp) {
    tm tmValue = {};
    istringstream ss(timestamp);
    ss >> get_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return timestamp;
    }
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%d %b %Y %I:%M %p", &tmValue);
    return string(buffer);
}

bool ExecuteSQL(const string &sql) {
    lock_guard<mutex> lock(dbMutex);
    char *errorMessage = nullptr;
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        cerr << "SQLite error: " << (errorMessage ? errorMessage : "unknown") << endl;
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

void WorkerLoop() {
    while (true) {
        SOCKET clientSocket;
        {
            unique_lock<mutex> lock(taskMutex);
            taskAvailable.wait(lock, [] {
                return shutdownPool || !taskQueue.empty();
            });
            if (shutdownPool && taskQueue.empty()) {
                return;
            }
            clientSocket = taskQueue.front();
            taskQueue.pop();
        }
        InteractWithClient(clientSocket);
    }
}

// (forward declarations moved earlier)


bool EnsureDeliveredColumn() {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "PRAGMA table_info(Messages);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    bool found = false;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *nameText = sqlite3_column_text(stmt, 1);
        if (nameText && string(reinterpret_cast<const char *>(nameText)) == "delivered") {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found) {
        return true;
    }
    return ExecuteSQL("ALTER TABLE Messages ADD COLUMN delivered INTEGER NOT NULL DEFAULT 0;");
}

bool EnsureUserLastSeenColumn() {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "PRAGMA table_info(Users);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    bool found = false;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *nameText = sqlite3_column_text(stmt, 1);
        if (nameText && string(reinterpret_cast<const char *>(nameText)) == "last_seen") {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found) {
        return true;
    }
    return ExecuteSQL("ALTER TABLE Users ADD COLUMN last_seen DATETIME;");
}

bool EnsureSeenColumn() {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "PRAGMA table_info(Messages);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    bool found = false;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *nameText = sqlite3_column_text(stmt, 1);
        if (nameText && string(reinterpret_cast<const char *>(nameText)) == "seen") {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found) {
        return true;
    }
    return ExecuteSQL("ALTER TABLE Messages ADD COLUMN seen INTEGER NOT NULL DEFAULT 0;");
}

bool InitDatabase() {
    const string sql =
        "CREATE TABLE IF NOT EXISTS Users ("
        "username TEXT PRIMARY KEY, "
        "password TEXT NOT NULL, "
        "last_seen DATETIME"
        ");"
        "CREATE TABLE IF NOT EXISTS Rooms ("
        "name TEXT PRIMARY KEY"
        ");"
        "CREATE TABLE IF NOT EXISTS RoomMembers ("
        "room TEXT NOT NULL, "
        "username TEXT NOT NULL, "
        "PRIMARY KEY(room, username), "
        "FOREIGN KEY(room) REFERENCES Rooms(name), "
        "FOREIGN KEY(username) REFERENCES Users(username)"
        ");"
        "CREATE TABLE IF NOT EXISTS Messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sender TEXT NOT NULL, "
        "recipient TEXT, "
        "room TEXT, "
        "body TEXT NOT NULL, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "is_private INTEGER NOT NULL, "
        "delivered INTEGER NOT NULL DEFAULT 0, "
        "FOREIGN KEY(sender) REFERENCES Users(username), "
        "FOREIGN KEY(recipient) REFERENCES Users(username), "
        "FOREIGN KEY(room) REFERENCES Rooms(name)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_messages_room ON Messages(room);"
        "CREATE INDEX IF NOT EXISTS idx_messages_sender ON Messages(sender);";
    if (!ExecuteSQL(sql)) {
        return false;
    }
    if (!EnsureDeliveredColumn()) {
        return false;
    }
    if (!EnsureUserLastSeenColumn()) {
        return false;
    }
    if (!EnsureSeenColumn()) {
        return false;
    }
    return true;
}

bool OpenDatabase() {
    int result = sqlite3_open_v2("chat.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        cerr << "Could not open SQLite database: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    return InitDatabase();
}

void CloseDatabase() {
    lock_guard<mutex> lock(dbMutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool InsertMessage(const string &sender, const string &recipient, const string &room, const string &body, bool isPrivate, bool delivered) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "INSERT INTO Messages(sender, recipient, room, body, is_private, delivered) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, sender.c_str(), -1, SQLITE_TRANSIENT);
    if (!recipient.empty()) {
        sqlite3_bind_text(stmt, 2, recipient.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (!room.empty()) {
        sqlite3_bind_text(stmt, 3, room.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, isPrivate ? 1 : 0);
    sqlite3_bind_int(stmt, 6, delivered ? 1 : 0);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) {
        cerr << "SQLite insert error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    return true;
}

bool MarkMessagesSeen(const string &recipient, const string &sender) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "UPDATE Messages SET seen = 1 WHERE recipient = ? AND sender = ? AND is_private = 1 AND seen = 0;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sender.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (result == SQLITE_DONE || result == SQLITE_ROW);
}

bool SearchMessages(const string &term, SOCKET clientSocket) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "SELECT id, sender, recipient, room, body, timestamp FROM Messages WHERE body LIKE ? ORDER BY timestamp DESC LIMIT 50;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    string pattern = "%" + term + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    int found = 0;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *senderText = sqlite3_column_text(stmt, 1);
        const unsigned char *recipientText = sqlite3_column_text(stmt, 2);
        const unsigned char *roomText = sqlite3_column_text(stmt, 3);
        const unsigned char *bodyText = sqlite3_column_text(stmt, 4);
        const unsigned char *timestampText = sqlite3_column_text(stmt, 5);
        string senderName = senderText ? reinterpret_cast<const char *>(senderText) : string();
        string recipientName = recipientText ? reinterpret_cast<const char *>(recipientText) : string();
        string roomName = roomText ? reinterpret_cast<const char *>(roomText) : string();
        string body = bodyText ? reinterpret_cast<const char *>(bodyText) : string();
        string timestamp = timestampText ? reinterpret_cast<const char *>(timestampText) : string();
        string line = to_string(id) + ": [" + timestamp + "] " + senderName + (recipientName.empty() ? (roomName.empty() ? "" : "@" + roomName) : " -> " + recipientName) + ": " + body + "\n";
        SendToClient(clientSocket, line);
        ++found;
    }
    sqlite3_finalize(stmt);
    if (found == 0) {
        SendToClient(clientSocket, "No messages found matching: " + term + "\n");
    }
    return true;
}

bool DeliverOfflineMessages(const string &username, SOCKET clientSocket) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "SELECT id, sender, body, timestamp FROM Messages WHERE recipient = ? AND is_private = 1 AND delivered = 0 ORDER BY timestamp;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    vector<int> pendingIds;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *senderText = sqlite3_column_text(stmt, 1);
        const unsigned char *bodyText = sqlite3_column_text(stmt, 2);
        const unsigned char *timestampText = sqlite3_column_text(stmt, 3);
        string senderName = senderText ? reinterpret_cast<const char *>(senderText) : string();
        string body = bodyText ? reinterpret_cast<const char *>(bodyText) : string();
        string timestamp = timestampText ? reinterpret_cast<const char *>(timestampText) : string();
        string message = "(offline) " + senderName + " -> you [" + timestamp + "]: " + body + "\n";
        SendToClient(clientSocket, message);
        pendingIds.push_back(id);
    }
    sqlite3_finalize(stmt);
    for (int id : pendingIds) {
        const char *updateSql = "UPDATE Messages SET delivered = 1 WHERE id = ?;";
        sqlite3_stmt *updateStmt = nullptr;
        int updateResult = sqlite3_prepare_v2(db, updateSql, -1, &updateStmt, nullptr);
        if (updateResult != SQLITE_OK) {
            cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
            continue;
        }
        sqlite3_bind_int(updateStmt, 1, id);
        sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);
    }
    return true;
}

bool RoomExists(const string &roomName) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "SELECT 1 FROM Rooms WHERE name = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    bool exists = (result == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool SaveRoom(const string &roomName) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "INSERT OR IGNORE INTO Rooms(name) VALUES (?);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool SaveRoomMember(const string &roomName, const string &username) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "INSERT OR IGNORE INTO RoomMembers(room, username) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool RemoveRoomMember(const string &roomName, const string &username) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "DELETE FROM RoomMembers WHERE room = ? AND username = ?;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool RemoveRoomIfEmpty(const string &roomName) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "DELETE FROM Rooms WHERE name = ? AND NOT EXISTS (SELECT 1 FROM RoomMembers WHERE room = ?);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, roomName.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool LoadUsers() {
    lock_guard<mutex> lock(usersMutex);
    storedUsers.clear();
    const char *sql = "SELECT username, password FROM Users;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *usernameText = sqlite3_column_text(stmt, 0);
        const unsigned char *passwordText = sqlite3_column_text(stmt, 1);
        if (usernameText && passwordText) {
            storedUsers[reinterpret_cast<const char *>(usernameText)] = reinterpret_cast<const char *>(passwordText);
        }
    }

    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool SaveUser(const string &username, const string &password) {
    lock_guard<mutex> lock(usersMutex);
    const char *sql = "INSERT INTO Users(username, password, last_seen) VALUES (?, ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) {
        cerr << "SQLite insert error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    storedUsers[username] = password;
    return true;
}

bool UpdateLastSeen(const string &username) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "UPDATE Users SET last_seen = CURRENT_TIMESTAMP WHERE username = ?;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        cerr << "SQLite prepare error: " << sqlite3_errmsg(db) << endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return result == SQLITE_DONE;
}

bool GetLastSeen(const string &username, string &response) {
    lock_guard<mutex> lock(dbMutex);
    const char *sql = "SELECT last_seen FROM Users WHERE username = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    result = sqlite3_step(stmt);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }
    const unsigned char *timestampText = sqlite3_column_text(stmt, 0);
    string timestamp = timestampText ? reinterpret_cast<const char *>(timestampText) : string();
    sqlite3_finalize(stmt);
    if (timestamp.empty()) {
        response = "No last seen information available for " + username + ".\n";
    } else {
        response = "Last active: " + FormatTimestamp(timestamp) + "\n";
    }
    return true;
}

bool RegisterUser(const string &username, const string &password, string &response) {
    lock_guard<mutex> lock(usersMutex);
    if (username.empty() || password.empty()) {
        response = "Usage: /register <username> <password>\n";
        return false;
    }
    if (storedUsers.find(username) != storedUsers.end()) {
        response = "Username already exists. Try another username or login.\n";
        return false;
    }
    if (!SaveUser(username, password)) {
        response = "Failed to save user credentials.\n";
        return false;
    }
    response = "Registration successful. You are now logged in as " + username + ".\n";
    return true;
}

bool AuthenticateUser(const string &username, const string &password, string &response) {
    lock_guard<mutex> lock(usersMutex);
    auto it = storedUsers.find(username);
    if (it == storedUsers.end()) {
        response = "Username not found. Please register first.\n";
        return false;
    }
    if (it->second != password) {
        response = "Invalid password. Please try again.\n";
        return false;
    }
    response = "Login successful. You are now logged in as " + username + ".\n";
    return true;
}

bool SendToClient(SOCKET clientSocket, const string &message) {
    int sent = send(clientSocket, message.c_str(), static_cast<int>(message.length()), 0);
    return sent != SOCKET_ERROR;
}

bool IsAuthenticated(SOCKET clientSocket) {
    lock_guard<mutex> lock(clientsMutex);
    return authenticatedUsers.find(clientSocket) != authenticatedUsers.end();
}

string GetAuthenticatedUsername(SOCKET clientSocket) {
    lock_guard<mutex> lock(clientsMutex);
    auto it = authenticatedUsers.find(clientSocket);
    return (it != authenticatedUsers.end()) ? it->second : string();
}

string GetClientRoom(SOCKET clientSocket) {
    lock_guard<mutex> lock(clientsMutex);
    auto it = clientRoom.find(clientSocket);
    return (it != clientRoom.end()) ? it->second : string();
}

bool CreateRoom(SOCKET clientSocket, const string &roomName, string &response) {
    if (roomName.empty()) {
        response = "Usage: /create <room-name>\n";
        return false;
    }

    if (RoomExists(roomName)) {
        response = "Room already exists: " + roomName + "\n";
        return false;
    }

    if (!SaveRoom(roomName)) {
        response = "Failed to create room: " + roomName + "\n";
        return false;
    }

    lock_guard<mutex> lock(clientsMutex);
    auto currentIt = clientRoom.find(clientSocket);
    if (currentIt != clientRoom.end()) {
        string currentRoom = currentIt->second;
        rooms[currentRoom].erase(clientSocket);
        if (rooms[currentRoom].empty()) {
            rooms.erase(currentRoom);
            RemoveRoomIfEmpty(currentRoom);
        }
    }

    rooms[roomName].insert(clientSocket);
    clientRoom[clientSocket] = roomName;
    SaveRoomMember(roomName, GetAuthenticatedUsername(clientSocket));
    response = "Room created and joined: " + roomName + "\n";
    return true;
}

bool JoinRoom(SOCKET clientSocket, const string &roomName, string &response) {
    if (roomName.empty()) {
        response = "Usage: /join <room-name>\n";
        return false;
    }

    if (!RoomExists(roomName)) {
        response = "Room does not exist: " + roomName + "\n";
        return false;
    }

    lock_guard<mutex> lock(clientsMutex);
    auto currentIt = clientRoom.find(clientSocket);
    if (currentIt != clientRoom.end() && currentIt->second == roomName) {
        response = "You are already in room " + roomName + ".\n";
        return false;
    }

    if (currentIt != clientRoom.end()) {
        string currentRoom = currentIt->second;
        rooms[currentRoom].erase(clientSocket);
        if (rooms[currentRoom].empty()) {
            rooms.erase(currentRoom);
            RemoveRoomIfEmpty(currentRoom);
        }
    }

    rooms[roomName].insert(clientSocket);
    clientRoom[clientSocket] = roomName;
    SaveRoomMember(roomName, GetAuthenticatedUsername(clientSocket));
    response = "Joined room " + roomName + "\n";
    return true;
}

bool LeaveRoom(SOCKET clientSocket, const string &roomName, string &response) {
    if (roomName.empty()) {
        response = "Usage: /leave <room-name>\n";
        return false;
    }

    lock_guard<mutex> lock(clientsMutex);
    auto currentIt = clientRoom.find(clientSocket);
    if (currentIt == clientRoom.end()) {
        response = "You are not in any room.\n";
        return false;
    }
    if (currentIt->second != roomName) {
        response = "You are not in room " + roomName + ".\n";
        return false;
    }

    rooms[roomName].erase(clientSocket);
    if (rooms[roomName].empty()) {
        rooms.erase(roomName);
    }
    clientRoom.erase(clientSocket);
    RemoveRoomMember(roomName, GetAuthenticatedUsername(clientSocket));
    RemoveRoomIfEmpty(roomName);
    response = "Left room " + roomName + "\n";
    return true;
}

SOCKET GetClientSocketByUsername(const string &username) {
    lock_guard<mutex> lock(clientsMutex);
    for (auto client : clients) {
        auto it = authenticatedUsers.find(client);
        if (it != authenticatedUsers.end() && it->second == username) {
            return client;
        }
    }
    return INVALID_SOCKET;
}

bool SendPrivateMessage(const string &sender, const string &recipient, const string &body, SOCKET senderSocket) {
    SOCKET recipientSocket = GetClientSocketByUsername(recipient);
    if (recipientSocket == INVALID_SOCKET) {
        return false;
    }
    string recipientMessage = "(private) " + sender + " -> you: " + body + "\n";
    string senderMessage = "(private) to " + recipient + ": " + body + "\n";
    if (!SendToClient(recipientSocket, recipientMessage)) {
        return false;
    }
    SendToClient(senderSocket, senderMessage);
    return true;
}

void BroadcastTyping(const string &username, const string &roomName, bool starting) {
    lock_guard<mutex> lock(clientsMutex);
    auto it = rooms.find(roomName);
    if (it == rooms.end()) return;
    string note = starting ? "(typing) " + username + " is typing...\n" : "(typing) " + username + " stopped typing.\n";
    for (auto client : it->second) {
        SendToClient(client, note);
    }
}

void RemoveClient(SOCKET clientSocket) {
    lock_guard<mutex> lock(clientsMutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (*it == clientSocket) {
            clients.erase(it);
            break;
        }
    }
    auto currentIt = clientRoom.find(clientSocket);
    if (currentIt != clientRoom.end()) {
        string currentRoom = currentIt->second;
        rooms[currentRoom].erase(clientSocket);
        if (rooms[currentRoom].empty()) {
            rooms.erase(currentRoom);
        }
        clientRoom.erase(currentIt);
    }
    authenticatedUsers.erase(clientSocket);
}

void BroadcastMessage(const string &message, SOCKET senderSocket, const string &roomName) {
    lock_guard<mutex> lock(clientsMutex);
    auto it = rooms.find(roomName);
    if (it == rooms.end()) {
        return;
    }
    for (auto client : it->second) {
        if (client != senderSocket) {
            SendToClient(client, message);
        }
    }
}

void InteractWithClient(SOCKET clientSocket) {
    cout << "Client connected" << endl;
    char buffer[4096];

    SendToClient(clientSocket, "Welcome! Use /register <username> <password> or /login <username> <password> to enter the chat.\n");
    SendToClient(clientSocket, "After login, manage rooms with /create <room-name>, /join <room-name>, and /leave <room-name>.\n");
    SendToClient(clientSocket, "Use /msg <username> <message> for private messages.\n");

    while (true) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            cout << "Client disconnected" << endl;
            break;
        }

        string message(buffer, bytesReceived);
        message = Trim(message);
        if (message.empty()) {
            continue;
        }

        cout << "Message from client: " << message << endl;

        if (!IsAuthenticated(clientSocket)) {
            auto tokens = SplitString(message);
            if (tokens.empty()) {
                SendToClient(clientSocket, "Please login or register before sending messages.\n");
                continue;
            }

            if (tokens[0] == "/register") {
                if (tokens.size() != 3) {
                    SendToClient(clientSocket, "Usage: /register <username> <password>\n");
                    continue;
                }
                string response;
                if (RegisterUser(tokens[1], tokens[2], response)) {
                    lock_guard<mutex> lock(clientsMutex);
                    authenticatedUsers[clientSocket] = tokens[1];
                    SendToClient(clientSocket, response);
                } else {
                    SendToClient(clientSocket, response);
                }
                continue;
            }

            if (tokens[0] == "/login") {
                if (tokens.size() != 3) {
                    SendToClient(clientSocket, "Usage: /login <username> <password>\n");
                    continue;
                }
                string response;
                if (AuthenticateUser(tokens[1], tokens[2], response)) {
                    lock_guard<mutex> lock(clientsMutex);
                    authenticatedUsers[clientSocket] = tokens[1];
                    UpdateLastSeen(tokens[1]);
                    SendToClient(clientSocket, response);
                    DeliverOfflineMessages(tokens[1], clientSocket);
                } else {
                    SendToClient(clientSocket, response);
                }
                continue;
            }

            SendToClient(clientSocket, "Please login or register using /login or /register.\n");
            continue;
        }

        string username = GetAuthenticatedUsername(clientSocket);
        if (message == "/quit") {
            SendToClient(clientSocket, "Goodbye!\n");
            break;
        }

        // Rate limiting: max 5 messages per 1 second window
        {
            lock_guard<mutex> rlock(rateMutex);
            auto &state = rateStates[clientSocket];
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - state.windowStart).count();
            if (elapsed > 1000) {
                state.count = 0;
                state.windowStart = now;
            }
            if (state.count >= 5) {
                SendToClient(clientSocket, "Rate limit exceeded: max 5 messages/sec\n");
                continue;
            }
            state.count++;
        }

        auto tokens = SplitString(message);
        if (!tokens.empty() && tokens[0] == "/typing") {
            if (tokens.size() == 2 && (tokens[1] == "start" || tokens[1] == "stop")) {
                string currentRoom = GetClientRoom(clientSocket);
                if (currentRoom.empty()) {
                    SendToClient(clientSocket, "Join a room to send typing indicators.\n");
                } else {
                    BroadcastTyping(username, currentRoom, tokens[1] == "start");
                }
            } else {
                SendToClient(clientSocket, "Usage: /typing start|stop\n");
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/search") {
            if (tokens.size() < 2) {
                SendToClient(clientSocket, "Usage: /search <term>\n");
            } else {
                size_t pos = message.find(' ');
                string term = (pos != string::npos) ? Trim(message.substr(pos + 1)) : string();
                if (!term.empty()) SearchMessages(term, clientSocket);
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/seen") {
            if (tokens.size() != 2) {
                SendToClient(clientSocket, "Usage: /seen <username>\n");
            } else {
                string other = tokens[1];
                if (MarkMessagesSeen(username, other)) {
                    SendToClient(clientSocket, "Marked messages from " + other + " as seen.\n");
                    SOCKET otherSocket = GetClientSocketByUsername(other);
                    if (otherSocket != INVALID_SOCKET) {
                        SendToClient(otherSocket, "(receipt) " + username + " has seen your messages.\n");
                    }
                } else {
                    SendToClient(clientSocket, "Failed to mark messages as seen or none to mark.\n");
                }
            }
            continue;
        }
        if (!tokens.empty() && tokens[0] == "/msg") {
            if (tokens.size() < 3) {
                SendToClient(clientSocket, "Usage: /msg <username> <message>\n");
                continue;
            }
            string target = tokens[1];
            size_t bodyStart = message.find(target, 5);
            string body = (bodyStart != string::npos)
                ? Trim(message.substr(bodyStart + target.length()))
                : string();
            if (body.empty()) {
                SendToClient(clientSocket, "Usage: /msg <username> <message>\n");
                continue;
            }
            if (!SendPrivateMessage(username, target, body, clientSocket)) {
                lock_guard<mutex> ulock(usersMutex);
                if (storedUsers.find(target) != storedUsers.end()) {
                    InsertMessage(username, target, "", body, true, false);
                    SendToClient(clientSocket, "Recipient is offline; message will be delivered when they login.\n");
                } else {
                    SendToClient(clientSocket, "User " + target + " is not online or does not exist.\n");
                }
            } else {
                InsertMessage(username, target, "", body, true, true);
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/create") {
            string response;
            if (tokens.size() != 2) {
                SendToClient(clientSocket, "Usage: /create <room-name>\n");
                continue;
            }
            if (CreateRoom(clientSocket, tokens[1], response)) {
                SendToClient(clientSocket, response);
            } else {
                SendToClient(clientSocket, response);
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/join") {
            string response;
            if (tokens.size() != 2) {
                SendToClient(clientSocket, "Usage: /join <room-name>\n");
                continue;
            }
            if (JoinRoom(clientSocket, tokens[1], response)) {
                SendToClient(clientSocket, response);
                BroadcastMessage(username + " joined room " + tokens[1] + ".\n", clientSocket, tokens[1]);
            } else {
                SendToClient(clientSocket, response);
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/leave") {
            string response;
            if (tokens.size() != 2) {
                SendToClient(clientSocket, "Usage: /leave <room-name>\n");
                continue;
            }
            if (LeaveRoom(clientSocket, tokens[1], response)) {
                SendToClient(clientSocket, response);
            } else {
                SendToClient(clientSocket, response);
            }
            continue;
        }

        if (!tokens.empty() && tokens[0] == "/lastseen") {
            if (tokens.size() != 2) {
                SendToClient(clientSocket, "Usage: /lastseen <username>\n");
                continue;
            }
            string response;
            if (GetLastSeen(tokens[1], response)) {
                SendToClient(clientSocket, response);
            } else {
                SendToClient(clientSocket, "User not found: " + tokens[1] + "\n");
            }
            continue;
        }

        string currentRoom = GetClientRoom(clientSocket);
        if (currentRoom.empty()) {
            SendToClient(clientSocket, "Join a room before sending messages. Use /create or /join.\n");
            continue;
        }

        string broadcast = username + "@" + currentRoom + ": " + message + "\n";
        BroadcastMessage(broadcast, clientSocket, currentRoom);
        InsertMessage(username, "", currentRoom, message, false, true);
        UpdateLastSeen(username);
    }

    RemoveClient(clientSocket);
    close(clientSocket);
}

bool Initialize() {
    // No initialization required on POSIX
    return true;
}

int main() {
    if (!Initialize()) {
        cerr << "Winsock initialization failed" << endl;
        return 1;
    }

    if (!OpenDatabase()) {
        cerr << "Failed to open SQLite database" << endl;
        return 1;
    }

    if (!LoadUsers()) {
        cerr << "Failed to load users from database" << endl;
        CloseDatabase();
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << strerror(errno) << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    inet_pton(AF_INET, "0.0.0.0", &serverAddr.sin_addr);

    if (::bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << strerror(errno) << endl;
        close(listenSocket);
        return 1;
    }

    if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed: " << strerror(errno) << endl;
        close(listenSocket);
        return 1;
    }

    unsigned int threadCount = thread::hardware_concurrency();
    if (threadCount == 0) {
        threadCount = 4;
    }
    for (unsigned int i = 0; i < threadCount; ++i) {
        workerThreads.emplace_back(WorkerLoop);
    }

    cout << "Server listening on port 12345" << endl;

    while (true) {
        SOCKET clientSocket = ::accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed: " << strerror(errno) << endl;
            continue;
        }

        {
            lock_guard<mutex> lock(clientsMutex);
            clients.push_back(clientSocket);
        }
        {
            lock_guard<mutex> taskLock(taskMutex);
            taskQueue.push(clientSocket);
        }
        taskAvailable.notify_one();
    }

    close(listenSocket);
    CloseDatabase();
    return 0;
}

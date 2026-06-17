This is a simple, multi-user real-time chat application built using sockets in C++. 
The system stores persistent state in a SQLite database located at `chat.db` in the server working directory. The server creates tables for `Users`, `Rooms`, `RoomMembers`, and `Messages` to persist users, room structure, membership, chat history, and offline private messages.
Users must authenticate with `/register <username> <password>` or `/login <username> <password>`, then they can manage rooms with `/create <room-name>`, `/join <room-name>`, and `/leave <room-name>`. 
Send private messages using `/msg <username> <message>`. Use `/lastseen <username>` to view a user's last active timestamp.
Offline private messages are stored when the recipient is offline and delivered automatically the next time they log in.
The chat server uses a thread pool with a task queue rather than spawning a new thread for every accepted connection. This improves scalability under high client load by limiting thread growth and reusing worker threads.

Modularization
--------------
This repository now includes a scaffolded modular layout to help split responsibilities and make the project more maintainable:

- `Server/` – server entry, accept loop, thread-pool, client & room managers, database code (scaffolds)
- `Client/` – client code scaffold for `client1` / `client2` consolidation
- `Common/` – shared models like `Message` and `User`

Next steps: move the functions from `server/main.cpp` into the appropriate module files and replace the scaffolds with the real implementations. I can continue the automated refactor if you'd like.

Architecture
------------
Clients
	↓
TCP Socket Layer
	↓
Server
 ├─ Authentication
 ├─ Room Manager
 ├─ Message Dispatcher
 └─ Database

Features
--------
- User Registration & Authentication
- Multi-room Chat Support
- Private Messaging
- Offline Message Delivery
- Last-Seen Tracking
- SQLite-based Persistent Storage
- Thread Pool for Concurrent Request Processing
- Thread-safe Shared State Management

Tech Stack
----------
- C++
- POSIX/WinSock Sockets
- SQLite
- Multithreading
- Mutex Synchronization
- Thread Pool Architecture

Scalability
-----------
The server uses a fixed-size thread pool and task queue
instead of spawning one thread per client connection.

Benefits:
- Reduced thread creation overhead
- Better resource utilization
- Improved scalability under concurrent load

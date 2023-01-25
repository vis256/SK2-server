#include <iostream>
#include <vector>
#include <map>
#include <thread>
#include <poll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <string.h>
#include <stdexcept>
#include <sstream>

#define HEARTBEAT_INTERVAL 10

// Define the maximum number of users that can be in a room
#define MAX_USERS_PER_ROOM 16

// Define user roles
enum class UserRole {
  USER,
  MUSICIAN
};

int xd = 0;


// Define user class
class User {
  public: 
  UserRole role_;

  // User(int socket, UserRole role): socket_(socket), role_(role) {}
  User(int socket, UserRole role) {
    socket_ = socket;
    role_ = role;
    id_ = xd++;
  }
  User(int Id, int socket, UserRole role): id_(Id), socket_(socket), role_(role) {}

  bool operator == (const User & other) {
    return socket_ == other.socket_;
  }

  bool operator != (const User & other) {
    return !( * this == other);
  }

  // Get user's socket descriptor
  int getSocket() {
    return socket_;
  }

  // Get user's role
  UserRole getRole() {
    return role_;
  }

  int getId() {
    return id_;
  }

  private: 
  int socket_;
  int id_;
};


// Define room class
class Room {
  public: std::vector < User > users_;

  int getUserCount() {
    return users_.size();
  }

  std::vector < User > & getUsers() {
    return users_;
  }

  Room(int id): id_(id) {}

  Room() {}
  // Get room's ID
  int getId() {
    return id_;
  }

  // Add a user to the room
  void addUser(User & user) {
    users_.push_back(user);
  }

  // Remove a user from the room
  void removeUser(User & user) {
    users_.erase(std::remove(users_.begin(), users_.end(), user), users_.end());
  }
void removeUserById(int userId) {
    for (auto it = users_.begin(); it != users_.end(); ++it) {
        if (it->getId() == userId) {
            users_.erase(it);
            break;
        }
    }
}
  // Broadcast a message to all users in the room
  void broadcast(const char * message, int messageSize) {
    for (auto & user: users_) {
      int socket = user.getSocket();
      write(socket, message, messageSize);
    }
  }

  // Check if the room is empty
  bool isEmpty() {
    return users_.empty();
  }

  private: int id_;
};

// Define the server class
class Server {
  public: Server(): nextRoomId_(0) {}

  // Start the server
  void start() {
    int port = 10000;
    std::cout << "[D] starting server..." << std::endl;
    // Create a socket
    int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);

    std::cout << "[D] socket descrptr=" << socketDescriptor << std::endl;
    
    // Bind the socket to an address and port
    sockaddr_in address {
      .sin_family = AF_INET, .sin_port = htons((short) port), .sin_addr = {
        INADDR_ANY
      }
    };

    int br = bind(socketDescriptor, (sockaddr * ) & address, sizeof(address));

    while (br == -1) {
      std::cout << "[D] " << port << " is taken, trying " << ++port << std::endl;
      address.sin_port = htons((short) port);
      br = bind(socketDescriptor, (sockaddr * ) & address, sizeof(address));
    }

    std::cout << "[D] bind=" << br << std::endl;

    // Listen for incoming connections
    int lr = listen(socketDescriptor, SOMAXCONN);

    std::cout << "[D] listen=" << lr << std::endl;
    // Set up the poll structure
    pollfd pollFd;
    pollFd.fd = socketDescriptor;
    pollFd.events = POLLIN;

    std::cout << "[!] listening on port " << port << std::endl;

    while (true) {
      // Wait for activity on the socket
      int pr = poll( &pollFd, 1, -1 );
      std::cout << "[D] poll=" << pr << std::endl;

      // Check for incoming connections
      sockaddr_in clientAddress;
      socklen_t clientAddressSize = sizeof(clientAddress);
		  int clientSocket;

      while ( (clientSocket = accept(socketDescriptor, nullptr, nullptr) ) != -1) {
      	std::cout << "[D] clientSckt=" << clientSocket << std::endl;
      	std::thread(&Server::handleRequests, this, clientSocket).detach();
      }

      std::cout << "[E] Accept failed" << std::endl;

      // Send heartbeat to all users to check if they are alive
      sendHeartbeat();
    }
  }

  private: std::map < int,
  Room > rooms_;
  int nextRoomId_;
void changeUserRole(int clientSocket, std::string request) {
    // Extract roomId, userId and role from the request string
    size_t space1 = request.find(' ');
    size_t space2 = request.substr(space1+1).find(' ');
    int roomId = std::stoi(request.substr(0, space1));
    int userId = std::stoi(request.substr(space1 + 1, space2 - space1 - 1));
    char role = request[space1 + space2 + 2];

    // Change the role of the user
    if (role == 'M') {
        rooms_[roomId].users_[userId].role_ = UserRole::MUSICIAN;
    } else if (role == 'L') {
        rooms_[roomId].users_[userId].role_ = UserRole::USER;
    } else {
        send(clientSocket, "Invalid role\n", 14, 0);
        return;
    }

    // Send a confirmation message to the client
    std::string message = "ROLE|";
    message += role;
    message += '\n';
    send(clientSocket, message.c_str(), message.length(), 0);
}


  // Send the list of active rooms to a client
  void sendRoomList(int clientSocket) {
    char message[1000] = "ROOMS|";
    // Build the list of room IDs
    for (auto
      const & it: rooms_) {
      char roomData[120];
      int uc = it.second.users_.size();
      sprintf(roomData, "%d %d/%d|", it.first, uc, MAX_USERS_PER_ROOM);
      strcat(message, roomData);
    };

    // Send the list to the client
    // std::cout << "sent: " << message << "size: " << strlen(message) << std::endl;
    send(clientSocket, message, strlen(message), 0);
  }

  void deleteUserFromAllRooms(int socketfd) {
    for (auto x : rooms_) {
      auto room = &x.second;
      for (User user : room->getUsers()) {
        if (user.getSocket() == socketfd) {
          room->removeUser(user);
          break;
        }
      }
    }
  }

  void broadcastMessage(int roomId, std::string message, int userId) {
    for (auto user : rooms_[roomId].users_) {
      if (user.getId() == userId) continue;
      send(user.getSocket(), message.c_str(), message.length(), 0);
    }
  }
  
  void sendNotes(int clientSocket, std::string request) {
    // Extract roomId, userId and note from the request string
    size_t space1 = request.find(' ');
    int roomId = std::stoi(request.substr(0, space1));
    size_t space2 = request.find(' ', space1 + 1);
    int userId = std::stoi(request.substr(space1 + 1, space2 - space1 - 1));
    std::string note = request.substr(space2+1);

    // Send a note to the client
    std::string message = "NOTE|" + note;
    message += '\n';
    broadcastMessage(roomId, message, userId);
  }

  void handleRequests(int clientSocket) {
    while (handleRequest(clientSocket)) {;};
  }

  bool checkHeartbeat(int requestSize, int clientSocket) {
    if (requestSize <= 0) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
      std::cout << "[!] " << clientSocket << " disconnected" << std::endl;
      deleteUserFromAllRooms(clientSocket);
      close(clientSocket);
      return false;
    } else {
      return true;
    }
  }

  // Handle a request to create or join a room
  bool handleRequest(int clientSocket) {
    // Wait for the client to send a request to create or join a room
    char requestBuffer[1024];
    int requestSize = recv(clientSocket, requestBuffer, sizeof(requestBuffer), 0);
    
    if (!checkHeartbeat(requestSize, clientSocket)) {
      return false;
    }

    std::string request = requestBuffer;

    std::cout << "[D] request=" << request << std::endl;
    // CREATE NEW ROOM
    if (request.substr(0, 6) == "create") {
      User user(clientSocket, UserRole::MUSICIAN);
      Room room(nextRoomId_++);
      room.addUser(user);
      rooms_[room.getId()] = room;
  
      char data[128];
      sprintf(data, "JOINNEW|%d %d", room.getId(), user.getId());

      send(clientSocket, data, strlen(data), 0);

    // JOIN ROOM AS LISTENER
    } else if (request.substr(0, 4) == "join") {
      int roomId = std::stoi(request.substr(5));
      
      if (rooms_.count(roomId) == 0) {
        send(clientSocket, "invalid room|", 13, 0);
        return true;
      }

      // Check if the room is full
      Room & room = rooms_[roomId];
      if (room.getUserCount() >= MAX_USERS_PER_ROOM) {
      send(clientSocket, "room full|", 10, 0);
      return true;
      }

      // Add the user to the room
      User user(clientSocket, UserRole::USER);
      room.addUser(user);

      char data[128];
      sprintf(data, "JOIN|%d %d", room.getId(), user.getId());

      send(clientSocket, data, strlen(data), 0);
    } 

    // LIST ALL ROOMS
    else if (request.substr(0, 4) == "list") {
      sendRoomList(clientSocket);
    } 

    // SEND NOTE DATA TO LISTENERS
    else if (request.substr(0, 4) == "NOTE") {
      sendNotes(clientSocket, request.substr(5));
    } 

    // CHANGE ROLE
    else if(request.substr(0, 11) == "change role"){
        changeUserRole(clientSocket, request.substr(12));
    } 

    // LEAVE ROOM
    else if(request.substr(0, 5) == "leave"){ 
      int roomId = std::stoi(request.substr(5,request.find(" ")));
      rooms_[roomId].removeUserById(clientSocket);
      removeEmptyRooms(roomId);
    } 

    // QUIT ie. DISCONNECT FROM SERVER
    else if (request.substr(0, 4) == "quit") {
      std::cout << "[D] Client quit" << std::endl;
      close(clientSocket);
      return false;
    }

    // ELSE: INVALID REQUEST
    else {
      // Invalid request
      send(clientSocket, "invalid request|", 17, 0);
      return true;
    }

    return true;
  }

  // Send a heartbeat to all users to check if they are alive
  void sendHeartbeat() {
    // for (auto& room : rooms_) {
    //     Room& current_room = room.second;
    //     for (auto& user : current_room.getUsers()) {
    //         int socket = user.getSocket();
    //         // send the message] requestSize=11
    //         int sent = send(socket, "heartbeat", 9, 0);
    //         if (sent <= 0)
    //         {
    //             std::cout << "User is disconnected" << std::endl;
    //             close(socket);
    //             current_room.removeUser(user);
    //         }
    //     }

    //     sleep(HEARTBEAT_INTERVAL * 1000);
    //     }
  }
  // Remove empty rooms from the server
  void removeEmptyRooms(int roomId) {
 
      if (rooms_[roomId].isEmpty()) {
        rooms_.erase(roomId);
      }
    
  }
};

int main(int argc, char * argv[]) {
  Server server;
  server.start();
  return 0;
}

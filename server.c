#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024
#define MAX_USERNAME_LEN 50

// Structure to hold client information
typedef struct {
  int socket;
  char username[MAX_USERNAME_LEN];
  int active;
} ClientInfo;

// Global client array
ClientInfo clients[MAX_CLIENTS] = {0};

// Broadcast message to all clients except sender
void broadcast_message(char *message, int sender_socket) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active && clients[i].socket != sender_socket) {
      send(clients[i].socket, message, strlen(message), 0);
    }
  }
}

// Find client index by socket
int find_client_index(int socket) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].socket == socket) {
      return i;
    }
  }
  return -1;
}

int main() {
  int server_fd, new_socket, client_socket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  char buffer[BUFFER_SIZE];
  fd_set readfds;

  // Create socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  // Configure server address
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // Bind socket to address
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  // Start listening
  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d\n", PORT);

  while (1) {
    // Clear socket set
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);
    int max_sd = server_fd;

    // Add child sockets to set
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clients[i].active) {
        FD_SET(clients[i].socket, &readfds);

        if (clients[i].socket > max_sd)
          max_sd = clients[i].socket;
      }
    }

    // Wait for activity on sockets
    int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      perror("select error");
      continue;
    }

    // New connection incoming
    if (FD_ISSET(server_fd, &readfds)) {
      if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                               (socklen_t *)&addrlen)) < 0) {
        perror("accept");
        continue;
      }

      // Receive username
      int username_len = recv(new_socket, buffer, BUFFER_SIZE, 0);
      buffer[username_len] = '\0';

      // Find first available slot
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
          clients[i].socket = new_socket;
          strcpy(clients[i].username, buffer);
          clients[i].active = 1;

          // Prepare join notification
          char join_msg[BUFFER_SIZE];
          snprintf(join_msg, sizeof(join_msg), ">>> %s joined the chat!",
                   clients[i].username);

          // Broadcast join message
          broadcast_message(join_msg, new_socket);

          printf("%s\n", join_msg);
          break;
        }
      }
    }

    // IO operation on client sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i].active)
        continue;

      client_socket = clients[i].socket;

      if (FD_ISSET(client_socket, &readfds)) {
        int valread = read(client_socket, buffer, BUFFER_SIZE);

        if (valread <= 0) {
          // Client disconnected
          char leave_msg[BUFFER_SIZE];
          snprintf(leave_msg, sizeof(leave_msg), ">>> %s left the chat.",
                   clients[i].username);

          // Broadcast leave message
          broadcast_message(leave_msg, client_socket);

          printf("%s\n", leave_msg);

          // Close socket and mark as inactive
          close(client_socket);
          clients[i].active = 0;
          memset(clients[i].username, 0, MAX_USERNAME_LEN);
        } else {
          // Normal message broadcast
          buffer[valread] = '\0';
          broadcast_message(buffer, client_socket);
        }
      }
    }
  }

  return 0;
}

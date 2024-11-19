#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_USERNAME_LEN 20

// ANSI color codes for terminal
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

// Global variables
int sock = 0;
char username[MAX_USERNAME_LEN];
pthread_t receive_thread;
struct termios orig_termios;

// Function to disable raw mode at exit
void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

// Function to enable raw mode for better input handling
void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Validate username
int validate_username(const char *name) {
  if (strlen(name) == 0 || strlen(name) > MAX_USERNAME_LEN) {
    return 0;
  }

  // Check for invalid characters
  for (int i = 0; name[i] != '\0'; i++) {
    if (!((name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= 'A' && name[i] <= 'Z') ||
          (name[i] >= '0' && name[i] <= '9') || name[i] == '_')) {
      return 0;
    }
  }
  return 1;
}

// Prompt for username with validation
void get_username() {
  while (1) {
    printf(ANSI_COLOR_YELLOW
           "Enter username (alphanumeric, max 20 chars): " ANSI_COLOR_RESET);
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    if (validate_username(username)) {
      break;
    }

    printf(ANSI_COLOR_RED "Invalid username. Try again.\n" ANSI_COLOR_RESET);
  }
}

// Handle receive messages
void *receive_messages(void *socket_desc) {
  char buffer[BUFFER_SIZE];
  int read_size;

  while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
    // Null terminate the buffer
    buffer[read_size] = '\0';

    // Move cursor to beginning of line
    printf("\r");

    // Clear entire line
    printf("\033[K");

    // Check if it's a system message (join/leave)
    if (strstr(buffer, ">>>") != NULL) {
      // System messages in yellow
      printf(ANSI_COLOR_YELLOW "%s\n" ANSI_COLOR_RESET, buffer);
    } else {
      // Regular messages in green
      printf(ANSI_COLOR_GREEN "%s\n" ANSI_COLOR_RESET, buffer);
    }

    // Reprint the current input prompt
    printf("%s: ", username);
    fflush(stdout);
  }

  // Connection lost
  printf(ANSI_COLOR_RED "\nConnection to server lost.\n" ANSI_COLOR_RESET);
  exit(1);
  return NULL;
}

// Handle sending messages
void send_messages() {
  char message[BUFFER_SIZE];
  char ch;
  int msg_len = 0;

  enable_raw_mode();

  printf(ANSI_COLOR_BLUE
         "Chat started. Press Ctrl-D to exit.\n" ANSI_COLOR_RESET);
  printf("%s: ", username);
  fflush(stdout);

  while (1) {
    // Read character by character
    if (read(STDIN_FILENO, &ch, 1) <= 0) {
      // EOF (Ctrl-D)
      printf(ANSI_COLOR_YELLOW "\nExiting chat...\n" ANSI_COLOR_RESET);
      close(sock);
      exit(0);
    }

    // Handle backspace
    if (ch == 127 || ch == '\b') {
      if (msg_len > 0) {
        printf("\b \b");
        msg_len--;
        fflush(stdout);
      }
      continue;
    }

    // Handle enter key
    if (ch == '\n' || ch == '\r') {
      if (msg_len > 0) {
        message[msg_len] = '\0';

        // Prepare full message
        char full_message[BUFFER_SIZE];
        snprintf(full_message, sizeof(full_message), "%s: %s", username,
                 message);

        // Send message
        send(sock, full_message, strlen(full_message), 0);

        // Reset message
        msg_len = 0;
        printf("\n%s: ", username);
        fflush(stdout);
      }
      continue;
    }

    // Add character to message
    if (msg_len < BUFFER_SIZE - 1) {
      message[msg_len++] = ch;
      printf("%c", ch);
      fflush(stdout);
    }
  }
}

int main() {
  struct sockaddr_in serv_addr;

  // Create socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror(ANSI_COLOR_RED "Socket creation error" ANSI_COLOR_RESET);
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  // Convert IPv4 address from text to binary
  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    printf(ANSI_COLOR_RED
           "\nInvalid address / Address not supported\n" ANSI_COLOR_RESET);
    return -1;
  }

  // Connect to server
  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf(ANSI_COLOR_RED "\nConnection Failed\n" ANSI_COLOR_RESET);
    return -1;
  }

  // Get username
  get_username();

  // Send username to server immediately after connection
  send(sock, username, strlen(username), 0);

  // Create thread to receive messages
  if (pthread_create(&receive_thread, NULL, receive_messages, (void *)&sock) <
      0) {
    printf(ANSI_COLOR_RED "Could not create receive thread\n" ANSI_COLOR_RESET);
    return -1;
  }

  // Send messages
  send_messages();

  // Close socket
  close(sock);
  return 0;
}

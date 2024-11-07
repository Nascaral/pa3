#include "http-server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define USERNAME_SIZE 16
#define MESSAGE_SIZE 256
#define TIMESTAMP_SIZE 20
#define MAX_REACTIONS 100
#define MAX_CHATS 100000

// Structs for Chat and Reaction
typedef struct {
    char user[USERNAME_SIZE];
    char message[USERNAME_SIZE];
} Reaction;

typedef struct {
    uint32_t id;
    char user[USERNAME_SIZE];
    char message[MESSAGE_SIZE];
    char timestamp[TIMESTAMP_SIZE];
    uint32_t num_reactions;
    Reaction reactions[MAX_REACTIONS];
} Chat;

// Global variables for storing chats
Chat chats[MAX_CHATS];
uint32_t chat_count = 0;

// Helper function to get the current timestamp
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M", tm_info);
}

// Function to add a chat
uint8_t add_chat(const char* username, const char* message) {
    if (chat_count >= MAX_CHATS || strlen(username) >= USERNAME_SIZE || strlen(message) >= MESSAGE_SIZE) {
        return 0; // Error: limits exceeded
    }

    Chat *chat = &chats[chat_count];
    chat->id = chat_count + 1;
    strncpy(chat->user, username, USERNAME_SIZE);
    strncpy(chat->message, message, MESSAGE_SIZE);
    get_timestamp(chat->timestamp, TIMESTAMP_SIZE);
    chat->num_reactions = 0;

    chat_count++;
    return 1; // Success
}

// Function to add a reaction
uint8_t add_reaction(const char* username, const char* reaction, uint32_t id) {
    if (id == 0 || id > chat_count || strlen(username) >= USERNAME_SIZE || strlen(reaction) >= USERNAME_SIZE) {
        return 0; // Error: invalid ID or limits exceeded
    }

    Chat *chat = &chats[id - 1];
    if (chat->num_reactions >= MAX_REACTIONS) {
        return 0; // Error: reaction limit exceeded
    }

    Reaction *new_reaction = &chat->reactions[chat->num_reactions++];
    strncpy(new_reaction->user, username, USERNAME_SIZE);
    strncpy(new_reaction->message, reaction, USERNAME_SIZE);

    return 1; // Success
}

// Function to reset all chats
void reset_chats() {
    chat_count = 0;
}

// Respond with chats
void respond_with_chats(int client) {
    char buffer[BUFFER_SIZE];
    int offset = 0;

    for (uint32_t i = 0; i < chat_count; i++) {
        Chat *chat = &chats[i];
        offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "[#%d %s] %s: %s\n",
                           chat->id, chat->timestamp, chat->user, chat->message);

        for (uint32_t j = 0; j < chat->num_reactions; j++) {
            Reaction *reaction = &chat->reactions[j];
            offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "    (%s) %s\n",
                               reaction->user, reaction->message);
        }
    }

    write(client, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n", 37);
    write(client, buffer, offset);
}

// Handle POST requests to add a new chat
void handle_post(char *path, int client) {
    char *user = strstr(path, "user=");
    char *message = strstr(path, "&message=");

    if (!user || !message) {
        write(client, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
        return;
    }

    user += 5; // Move pointer past "user="
    message += 9; // Move pointer past "&message="

    char username[USERNAME_SIZE];
    char msg[MESSAGE_SIZE];
    strncpy(username, user, strchr(user, '&') - user);
    strncpy(msg, message, strchr(message, ' ') - message);

    if (!add_chat(username, msg)) {
        write(client, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
        return;
    }

    respond_with_chats(client);
}

// Handle REACTION requests to add a reaction
void handle_reaction(char *path, int client) {
    char *user = strstr(path, "user=");
    char *reaction = strstr(path, "&message=");
    char *id = strstr(path, "&id=");

    if (!user || !reaction || !id) {
        write(client, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
        return;
    }

    user += 5;
    reaction += 9;
    id += 4;

    char username[USERNAME_SIZE];
    char reaction_text[USERNAME_SIZE];
    uint32_t chat_id = atoi(id);

    strncpy(username, user, strchr(user, '&') - user);
    strncpy(reaction_text, reaction, strchr(reaction, '&') - reaction);

    if (!add_reaction(username, reaction_text, chat_id)) {
        write(client, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
        return;
    }

    respond_with_chats(client);
}

// Handle /reset requests
void handle_reset(int client) {
    reset_chats();
    write(client, "HTTP/1.1 200 OK\r\n\r\n", 19);
}

// Main request handler
void handle_request(char *request, int client) {
    printf("Received request: %s\n", request);

    if (strncmp(request, "GET /post?", 10) == 0) {
        handle_post(request + 4, client);
    } else if (strncmp(request, "GET /react?", 11) == 0) {
        handle_reaction(request + 4, client);
    } else if (strncmp(request, "GET /chats", 10) == 0) {
        respond_with_chats(client);
    } else if (strncmp(request, "GET /reset", 10) == 0) {
        handle_reset(client);
    } else {
        write(client, "HTTP/1.1 404 Not Found\r\n\r\n", 26);
    }
}

// Main entry point
int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 0;
    start_server(&handle_request, port);
    return 0;
}

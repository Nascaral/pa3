#include "http-server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define USERNAME_SIZE 16
#define MESSAGE_SIZE 256
#define TIMESTAMP_SIZE 20
#define MAX_REACTIONS 100
#define MAX_CHATS 100000
#define BUFFER_SIZE 4096

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

// Globals
Chat chats[MAX_CHATS];
uint32_t chat_count = 0;

// Utility functions

// Decode URL-encoded string
void url_decode(char *dest, const char *src, size_t max_len) {
    size_t len = 0;
    while (*src && len < max_len - 1) {
        if (*src == '%' && isxdigit(src[1]) && isxdigit(src[2])) {
            *dest++ = (char)((src[1] - (isdigit(src[1]) ? '0' : 'A' - 10)) * 16 +
                             (src[2] - (isdigit(src[2]) ? '0' : 'A' - 10)));
            src += 3;
        } else if (*src == '+') {
            *dest++ = ' ';
            src++;
        } else {
            *dest++ = *src++;
        }
        len++;
    }
    *dest = '\0';
}

// Get formatted timestamp
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

// Extract a parameter from a request
int extract_param(const char *source, const char *param, char *dest, size_t dest_size) {
    const char *start = strstr(source, param);
    if (!start) return 0;
    start += strlen(param);
    const char *end = strchr(start, '&');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (length >= dest_size) return 0;
    strncpy(dest, start, length);
    dest[length] = '\0';
    url_decode(dest, dest, dest_size);
    return 1;
}

// Chat and reaction management

// Initialize a new chat
int add_chat(const char *username, const char *message) {
    if (chat_count >= MAX_CHATS || strlen(username) >= USERNAME_SIZE || strlen(message) >= MESSAGE_SIZE) {
        return 0;
    }
    Chat *chat = &chats[chat_count++];
    chat->id = chat_count;
    strncpy(chat->user, username, USERNAME_SIZE - 1);
    chat->user[USERNAME_SIZE - 1] = '\0';
    strncpy(chat->message, message, MESSAGE_SIZE - 1);
    chat->message[MESSAGE_SIZE - 1] = '\0';
    get_timestamp(chat->timestamp, TIMESTAMP_SIZE);
    chat->num_reactions = 0;
    return 1;
}

// Add reaction to a chat
int add_reaction(const char *username, const char *response, uint32_t id) {
    if (id == 0 || id > chat_count || strlen(username) >= USERNAME_SIZE || strlen(response) >= USERNAME_SIZE) {
        return 0;
    }
    Chat *chat = &chats[id - 1];
    if (chat->num_reactions >= MAX_REACTIONS) return 0;
    Reaction *reaction = &chat->reactions[chat->num_reactions++];
    strncpy(reaction->user, username, USERNAME_SIZE - 1);
    reaction->user[USERNAME_SIZE - 1] = '\0';
    strncpy(reaction->message, response, USERNAME_SIZE - 1);
    reaction->message[USERNAME_SIZE - 1] = '\0';
    return 1;
}

// Reset chats
void reset_chats() {
    chat_count = 0;
}

// Response handling

// Send the chat history to the client
void send_chat_history(int client) {
    char buffer[BUFFER_SIZE];
    int offset = snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n");

    for (uint32_t i = 0; i < chat_count; i++) {
        Chat *chat = &chats[i];
        offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "[#%d %s] %s: %s\n", 
                           chat->id, chat->timestamp, chat->user, chat->message);
        for (uint32_t j = 0; j < chat->num_reactions; j++) {
            Reaction *reaction = &chat->reactions[j];
            offset += snprintf(buffer + offset, BUFFER_SIZE - offset, "    (%s) %s\n", 
                               reaction->user, reaction->message);
        }
        if (offset >= BUFFER_SIZE - 256) {
            write(client, buffer, offset);
            offset = 0;
        }
    }
    if (offset > 0) write(client, buffer, offset);
}

// Request handlers

// Handle chat post
void handle_post(char *path, int client) {
    char username[USERNAME_SIZE], message[MESSAGE_SIZE];
    if (!extract_param(path, "user=", username, USERNAME_SIZE) || 
        !extract_param(path, "&message=", message, MESSAGE_SIZE)) {
        write(client, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
        return;
    }
    if (!add_chat(username, message)) {
        write(client, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
        return;
    }
    send_chat_history(client);
}

// Handle reaction post
void handle_react(char *path, int client) {
    char username[USERNAME_SIZE], reaction_text[USERNAME_SIZE], id_str[10];
    if (!extract_param(path, "user=", username, USERNAME_SIZE) ||
        !extract_param(path, "&message=", reaction_text, USERNAME_SIZE) ||
        !extract_param(path, "&id=", id_str, sizeof(id_str))) {
        write(client, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
        return;
    }
    uint32_t chat_id = (uint32_t)atoi(id_str);
    if (!add_reaction(username, reaction_text, chat_id)) {
        write(client, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 36);
        return;
    }
    send_chat_history(client);
}

// Handle reset
void handle_reset(int client) {
    reset_chats();
    write(client, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nChats have been reset.\n", 76);
}

// Handle requests
void handle_request(char *request, int client) {
    printf("Received request: %s\n", request);

    if (strncmp(request, "GET /post?", 10) == 0) {
        handle_post(request + 4, client);
    } else if (strncmp(request, "GET /react?", 11) == 0) {
        handle_react(request + 4, client);
    } else if (strncmp(request, "GET /chats", 10) == 0) {
        send_chat_history(client);
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

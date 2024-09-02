#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <ctype.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_ROOMS 5
#define MAX_USERS_PER_ROOM 5
#define HISTORY_SIZE 10

int server_socket = 0;

// Define global variables to store program name and arguments
char *program_name;
char **program_args;

// Structure for a message
typedef struct {
    char message[BUFFER_SIZE];
    char sender[BUFFER_SIZE];
} Message;

// Structure for a room
typedef struct {
    int clients[MAX_USERS_PER_ROOM];
    int count;
    Message history[HISTORY_SIZE];
    int history_count;
} Room;

// Structure for a client
typedef struct {
    int socket;
    char pseudo[BUFFER_SIZE];
} Client;

// Define emoticons
typedef struct {
    char *sequence;
    char *emoticon;
} EmoticonMap;

EmoticonMap emoticons[] = {
    {"smile", "😊"},
    {"laugh", "😂"},
    {"wink", "😉"},
    {"blush", "😊"},
    {"heart-eyes", "😍"},
    {"joy", "😅"},
    {"yum", "😋"},
    {"sunglasses", "😎"},
    {"stuck-out-tongue-winking-eye", "😜"},
    {"sleepy", "😪"},
    {"joy-cat", "😹"},
    {"kiss-heart", "😘"},
    {"thumbs-up", "👍"},
    {"thumbs-down", "👎"},
    {"fist", "✊"},
    {"raised-fist", "✊"},
    {"wave", "👋"},
    {"clap", "👏"},
    {"handshake", "🤝"},
    {"star", "⭐️"},
    {"sun", "☀️"},
    {"moon", "🌙"},
    {"rainbow", "🌈"},
    {"flower", "🌸"},
    {"gift", "🎁"},
    {"cake", "🍰"},
    {"pizza", "🍕"},
    {"hamburger", "🍔"},
    {"fries", "🍟"},
    {"ice-cream", "🍦"},
    {"coffee", "☕️"},
    {"beer", "🍺"},
    {"wine", "🍷"},
    {"cocktail", "🍹"},
    {"bottle", "🍾"},
    {"balloon", "🎈"},
    {"music", "🎵"},
    {"movie", "🎥"},
    {"computer", "💻"},
    {"phone", "📱"},
    {"book", "📚"},
    {"money", "💵"},
    {"calendar", "📅"},
    {"house", "🏠"},
    {"car", "🚗"},
    {"airplane", "✈️"},
    {"boat", "⛵️"},
    {"train", "🚂"},
    {"happy", "😄"},
    {"sad", "😞"},
    {"angry", "😡"},
    {"surprised", "😮"},
    {"tired", "😴"},
    {"sick", "🤒"},
    {"crazy", "😜"},
    {"scared", "😱"},
    {"love", "😍"},
    {"party", "🎉"},
    {"work", "💼"},
    {"study", "📖"},
    {"sport", "⚽️"},
    {"game", "🎮"},
    {"holiday", "🏖️"},
    {"health", "🏥"},
    {"school", "🏫"},
    {"euro", "💶"},
    {"dollar", "💵"},
    {"pound", "💷"},
    {"roll", "🍣"},
    {"sushi", "🍱"},
    {"rice-ball", "🍙"},
    {"pine-decoration", "🎍"},
    {"drink", "🍸"},
    {"strawberry", "🍓"},
    {"lemon", "🍋"},
    {"peach", "🍑"},
    {"cherries", "🍒"},
    {"chestnut", "🌰"},
};

#define EMOTICON_COUNT (sizeof(emoticons) / sizeof(EmoticonMap))

// Global declarations
sem_t mutex;
Room rooms[MAX_ROOMS];

// Convert text to emoticons
void convert_text_to_emoticons(char *buffer) {
    for (int i = 0; i < EMOTICON_COUNT; ++i) {
        char *pos;
        while ((pos = strstr(buffer, emoticons[i].sequence)) != NULL) {
            size_t len_sequence = strlen(emoticons[i].sequence);
            size_t len_emoticon = strlen(emoticons[i].emoticon);
            memmove(pos + len_emoticon, pos + len_sequence, strlen(pos + len_sequence) + 1);
            memcpy(pos, emoticons[i].emoticon, len_emoticon);
        }
    }
}

// Check if a string is a number
int is_number(const char *str) {
    while (*str) {
        if (!isdigit(*str)) {
            return 0;
        }
        str++; 
    }
    return 1; 
}

// Signal handler for SIGINT
void sigIntHandler(int sig) {
    printf(">>> SIGINT received [%d]. Shutting down server...\n", sig);
    exit(EXIT_SUCCESS);
}

// Signal handler for SIGHUP
void sigHupHandler(int sig) {
    printf(">>> SIGHUP received [%d]. Reloading program...\n", sig);
    execvp(program_name, program_args);
    perror("execvp failed");
    exit(EXIT_FAILURE);
}

// Cleanup before exit
void exitFunction() {
    printf("Cleaning up before exiting...\n");
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Server shutdown failed");
    }
    sem_destroy(&mutex);
    close(server_socket);
}

// Initialize server address
void initAdresse(struct sockaddr_in *adresse) {
    adresse->sin_family = AF_INET;
    adresse->sin_addr.s_addr = INADDR_ANY;
    adresse->sin_port = htons(PORT);
}

// Initialize socket
int initSocket(struct sockaddr_in *adresse) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("Failed to set socket options");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (bind(sock, (struct sockaddr*)adresse, sizeof(*adresse)) < 0) {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 3) < 0) {
        perror("Listen failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

// Handle client connections
void* handle_client(void* client_socket) {
    Client client;
    client.socket = *(int*)client_socket;
    char buffer[BUFFER_SIZE];
    int n = 0;
    int room_id = -1;

    // Request pseudo
    write(client.socket, "Enter your pseudo: ", strlen("Enter your pseudo: "));
    read(client.socket, buffer, sizeof(buffer));
    buffer[strcspn(buffer, "\r\n")] = 0; // Remove newline characters
    strcpy(client.pseudo, buffer);

    // Choose a room
    while (room_id < 0 || room_id >= MAX_ROOMS || rooms[room_id].count >= MAX_USERS_PER_ROOM) {
        bzero(buffer, BUFFER_SIZE);
        sprintf(buffer, "Please choose a room (0-%d): ", MAX_ROOMS - 1);
        write(client.socket, buffer, strlen(buffer));
        bzero(buffer, BUFFER_SIZE);
        read(client.socket, buffer, sizeof(buffer));
        buffer[strcspn(buffer, "\r\n")] = 0; // Remove newline characters

        if (strlen(buffer) == 0 || !is_number(buffer)) {
            write(client.socket, "Please enter a valid number.\n", strlen("Please enter a valid number.\n"));
            continue;
        }

        room_id = atoi(buffer);

        if (room_id < 0 || room_id >= MAX_ROOMS || rooms[room_id].count >= MAX_USERS_PER_ROOM) {
            sprintf(buffer, "Please enter a value between 0 and %d for an available room.\n", MAX_ROOMS - 1);
            write(client.socket, buffer, strlen(buffer));
        }
    }

    sem_wait(&mutex);
    rooms[room_id].clients[rooms[room_id].count++] = client.socket;

    // Send the message history to the new client
    for (int i = 0; i < rooms[room_id].history_count; ++i) {
        sprintf(buffer, "%s: %s", rooms[room_id].history[i].sender, rooms[room_id].history[i].message);
        write(client.socket, buffer, strlen(buffer));
    }
    sem_post(&mutex);

    sprintf(buffer, "You are in room %d\n", room_id);
    write(client.socket, buffer, strlen(buffer));

    while ((n = read(client.socket, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("Client %s (room %d): %s\n", client.pseudo, room_id, buffer);
        convert_text_to_emoticons(buffer);

        sem_wait(&mutex);

        if (rooms[room_id].history_count < HISTORY_SIZE) {
            strcpy(rooms[room_id].history[rooms[room_id].history_count].message, buffer);
            strcpy(rooms[room_id].history[rooms[room_id].history_count].sender, client.pseudo);
            rooms[room_id].history_count++;
        } else {
            for (int i = 0; i < HISTORY_SIZE - 1; ++i) {
                strcpy(rooms[room_id].history[i].message, rooms[room_id].history[i + 1].message);
                strcpy(rooms[room_id].history[i].sender, rooms[room_id].history[i + 1].sender);
            }
            strcpy(rooms[room_id].history[HISTORY_SIZE - 1].message, buffer);
            strcpy(rooms[room_id].history[HISTORY_SIZE - 1].sender, client.pseudo);
        }

        char message_to_send[BUFFER_SIZE];
        sprintf(message_to_send, "%s->%s", client.pseudo, buffer);

        for (int i = 0; i < rooms[room_id].count; i++) {
            if (rooms[room_id].clients[i] != client.socket) {
                write(rooms[room_id].clients[i], message_to_send, strlen(message_to_send));
            }
        }

        sem_post(&mutex);
    }

    close(client.socket);
    free(client_socket);
    return NULL;
}

// Main function
int main(int argc, char *argv[]) {
    program_name = argv[0];
    program_args = argv;

    // Check if the program should run in daemon mode
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    if (daemon_mode) {
        // Fork the process to create a daemon
        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }

        if (pid > 0) {
            // Parent process exits, leaving the child process running as a daemon
            exit(EXIT_SUCCESS);
        }

        // Child process (daemon)
        umask(0); // Set file permissions mask
        if (setsid() < 0) { // Create a new session
            perror("setsid failed");
            exit(EXIT_FAILURE);
        }

        // Redirect standard files to /dev/null
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        // In daemon mode, ignore the SIGHUP signal
        signal(SIGHUP, SIG_IGN);

    } else {
        // Normal mode: handle SIGHUP for reloading
        signal(SIGHUP, sigHupHandler);
    }

    // Display the PID of the program when it starts
    printf("Server starting with PID: %d\n", getpid());

    int client_socket = 0;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    initAdresse(&server_addr);
    server_socket = initSocket(&server_addr);

    signal(SIGINT, sigIntHandler);
    atexit(exitFunction);

    sem_init(&mutex, 0, 1);

    for (int i = 0; i < MAX_ROOMS; i++) {
        rooms[i].count = 0;
        rooms[i].history_count = 0;
    }

    printf("Waiting for incoming connections...\n");

    while ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len))) {
        printf("Connection accepted\n");
        pthread_t client_thread;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_socket;

        if (pthread_create(&client_thread, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            close(client_socket);
            continue;
        }
        pthread_detach(client_thread);
    }

    if (client_socket < 0) {
        perror("Accept failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    close(server_socket);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_ROOMS 5
#define MAX_USERS_PER_ROOM 5
#define HISTORY_SIZE 10  // Taille de l'historique des messages par room

// Structure pour un message
typedef struct {
    char message[BUFFER_SIZE];
    char sender[BUFFER_SIZE];
} Message;

// Structure pour une room
typedef struct {
    int clients[MAX_USERS_PER_ROOM];
    int count;
    Message history[HISTORY_SIZE];
    int history_count;
} Room;

// Structure pour un client
typedef struct {
    int socket;
    char pseudo[BUFFER_SIZE];
} Client;

// Déclarations globales
sem_t mutex;
Room rooms[MAX_ROOMS];

// Gestion des signaux
void sigIntHandler(int sig) {
    printf(">>> SIGINT received [%d]. Shutting down server...\n", sig);
    exit(EXIT_SUCCESS);
}

void exitFunction(int server_socket) {
    printf("Sortie de la session...\n");

    // Set SO_REUSEADDR option on the server socket
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Le serveur vient de fermer... ");
    }

    sem_destroy(&mutex);
    close(server_socket);
}


// Fonction pour gérer les clients une fois connecté au serveur 
void* handle_client(void* client_socket) {
    Client client;
    
    client.socket = *(int*)client_socket;
    char buffer[BUFFER_SIZE];
    int n;
    int room_id = -1;

    // Demander le pseudo au client
    write(client.socket, "Entrez votre pseudo: ", strlen("Entrez votre pseudo: "));
    read(client.socket, buffer, sizeof(buffer));
    strcpy(client.pseudo, buffer);

    // Choix de la room
    while (room_id < 0 || room_id >= MAX_ROOMS || rooms[room_id].count >= MAX_USERS_PER_ROOM) {
        bzero(buffer, BUFFER_SIZE);
        sprintf(buffer, "Veuillez choisir une room (0-%d): ", MAX_ROOMS - 1);
        write(client.socket, buffer, strlen(buffer));
        bzero(buffer, BUFFER_SIZE);
        read(client.socket, buffer, sizeof(buffer));
        room_id = atoi(buffer);
    }

    sem_wait(&mutex);
    rooms[room_id].clients[rooms[room_id].count++] = client.socket;
    
    // Envoyer l'historique des messages au nouveau client
    for (int i = 0; i < rooms[room_id].history_count; ++i) {
        sprintf(buffer, "%s: %s", rooms[room_id].history[i].sender, rooms[room_id].history[i].message);
        write(client.socket, buffer, strlen(buffer));
    }
    sem_post(&mutex);

    sprintf(buffer, "Vous êtes dans la room %d\n", room_id);
    write(client.socket, buffer, strlen(buffer));

while ((n = read(client.socket, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = '\0';
    printf("Client %s (room %d): %s\n", client.pseudo, room_id, buffer);

    // Diffuser le message à tous les autres clients de la même room
    sem_wait(&mutex);
    // Ajouter le message à l'historique de la room
    if (rooms[room_id].history_count < HISTORY_SIZE) {
        strcpy(rooms[room_id].history[rooms[room_id].history_count].message, buffer);
        strcpy(rooms[room_id].history[rooms[room_id].history_count].sender, client.pseudo);
        rooms[room_id].history_count++;
    } else {
        // Décaler les messages dans l'historique circulaire
        for (int i = 0; i < HISTORY_SIZE - 1; ++i) {
            strcpy(rooms[room_id].history[i].message, rooms[room_id].history[i + 1].message);
            strcpy(rooms[room_id].history[i].sender, rooms[room_id].history[i + 1].sender);
        }
        strcpy(rooms[room_id].history[HISTORY_SIZE - 1].message, buffer);
        strcpy(rooms[room_id].history[HISTORY_SIZE - 1].sender, client.pseudo);
    }
    
    // Préparer le message à diffuser aux autres clients
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

int main() {
    int server_socket, client_socket, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Gestion des signaux
    signal(SIGINT, sigIntHandler);
    atexit(exitFunction);

    // Initialisation du sémaphore
    sem_init(&mutex, 0, 1);

    // Initialisation des rooms
    for (int i = 0; i < MAX_ROOMS; i++) {
        rooms[i].count = 0;
        rooms[i].history_count = 0;
    }

    // Création du socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Could not create socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket created...\n");

    // Préparation de la structure sockaddr_in
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Liaison du socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("Bind done...\n");

    // Écoute des connexions entrantes
    if (listen(server_socket, 3) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("Waiting for incoming connections...\n");

    // Acceptation des connexions entrantes
    while ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len))) {
        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_t client_thread;
        new_sock = malloc(sizeof(int));
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

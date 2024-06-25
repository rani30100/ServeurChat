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

// Déclarations globales
sem_t mutex;
int clients[MAX_CLIENTS];
int client_count = 0;

// Gestion des signaux
void sigIntHandler(int sig) {
    printf(">>> SIGINT received [%d]. Shutting down server...\n", sig);
    exit(EXIT_SUCCESS);
}

void exitFunction() {
    printf("Exiting...\n");
    sem_destroy(&mutex);
}

// Fonction pour gérer la communication avec chaque client
void* handle_client(void* client_socket) {
    int sock = *(int*)client_socket;
    char buffer[BUFFER_SIZE];
    int n;

    while ((n = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        printf("Client %d: %s\n", sock, buffer);

        // Diffuse le message à tous les autres clients
        sem_wait(&mutex);
        for (int i = 0; i < client_count; i++) {
            if (clients[i] != sock) {
                write(clients[i], buffer, n);
            }
        }
        sem_post(&mutex);
    }

    close(sock);
    sem_wait(&mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == sock) {
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
    sem_post(&mutex);
    free(client_socket);
    return NULL;
}

// Fonction principale
int main() {
    int server_socket, client_socket, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Gestion des signaux
    signal(SIGINT, sigIntHandler);
    atexit(exitFunction);

    // Initialisation du sémaphore
    sem_init(&mutex, 0, 1);

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

        sem_wait(&mutex);
        clients[client_count++] = client_socket;
        sem_post(&mutex);

        pthread_t client_thread;
        new_sock = malloc(1);
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

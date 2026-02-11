#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 2323
#define BUFFER_SIZE 256

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr, local_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    socklen_t local_addr_len = sizeof(local_addr);
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    char local_ip[INET_ADDRSTRLEN];

    // Crea socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Errore: Impossibile creare socket");
        return 1;
    }

    // Configura indirizzo server
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore: Bind fallito");
        close(server_socket);
        return 1;
    }

    // Listen
    if (listen(server_socket, 3) < 0) {
        perror("Errore: Listen fallito");
        close(server_socket);
        return 1;
    }

    printf("Server in ascolto sulla porta %d...\n", PORT);

    while (1) {
        // Accetta connessione
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Errore: Accept fallito");
            continue;
        }

        // Ottieni IP e porta del client
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        // Ottieni IP e porta locale
        int local_port;
        if (getsockname(client_socket, (struct sockaddr*)&local_addr, &local_addr_len) == 0) {
            inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, INET_ADDRSTRLEN);
            local_port = ntohs(local_addr.sin_port);
        } else {
            strcpy(local_ip, "unknown");
            local_port = 0;
        }

        printf("Connessione ricevuta da %s:%d\n", client_ip, client_port);

        // Prepara messaggio da inviare
        snprintf(buffer, BUFFER_SIZE, "IP chiamante: %s\r\nPorta chiamante: %d\r\nIP locale: %s\r\nPorta locale: %d\r\n", client_ip, client_port, local_ip, local_port);

        // Invia messaggio al client
        send(client_socket, buffer, strlen(buffer), 0);

        // Chiudi connessione
        close(client_socket);
        printf("Connessione chiusa.\n\n");
    }

    // Cleanup
    close(server_socket);

    return 0;
}

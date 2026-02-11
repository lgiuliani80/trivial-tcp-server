#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/timerfd.h>

#define PORT 2323
#define BUFFER_SIZE 256
#define MAX_CLIENTS 1000

typedef enum {
    STATE_SENDING,
    STATE_WAITING,
    STATE_CLOSING
} ClientState;

typedef struct {
    int fd;
    int timer_fd;
    struct sockaddr_in addr;
    ClientState state;
    time_t send_time;
    int delay_us;
    char message[BUFFER_SIZE];
    int message_len;
    int bytes_sent;
} ClientInfo;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void format_timestamp(char *buffer, size_t size) {
    time_t now;
    struct tm *tm_info;
    time(&now);
    tm_info = gmtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
}

int main() {
    int server_socket;
    struct sockaddr_in server_addr;
    ClientInfo clients[MAX_CLIENTS];
    struct pollfd poll_fds[MAX_CLIENTS + 1];
    int num_clients = 0;
    char timestamp[32];
    
    srand(time(NULL));
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].timer_fd = -1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Errore: Impossibile creare socket");
        return 1;
    }
    
    set_nonblocking(server_socket);
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore: Bind fallito");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 50) < 0) {
        perror("Errore: Listen fallito");
        close(server_socket);
        return 1;
    }

    printf("Server in ascolto sulla porta %d (async)...\n", PORT);

    while (1) {
        poll_fds[0].fd = server_socket;
        poll_fds[0].events = POLLIN;
        
        int nfds = 1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                poll_fds[nfds].fd = clients[i].fd;
                poll_fds[nfds].events = (clients[i].state == STATE_SENDING) ? POLLOUT : 0;
                nfds++;
                
                if (clients[i].timer_fd != -1) {
                    poll_fds[nfds].fd = clients[i].timer_fd;
                    poll_fds[nfds].events = POLLIN;
                    nfds++;
                }
            }
        }
        
        int poll_result = poll(poll_fds, nfds, -1);
        
        if (poll_result < 0) {
            perror("Poll error");
            break;
        }
        
        // Accept new connections
        if (poll_fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
            
            if (client_fd >= 0) {
                set_nonblocking(client_fd);
                
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) {
                        slot = i;
                        break;
                    }
                }
                
                if (slot != -1) {
                    clients[slot].fd = client_fd;
                    clients[slot].timer_fd = -1;
                    clients[slot].addr = client_addr;
                    clients[slot].state = STATE_SENDING;
                    clients[slot].bytes_sent = 0;
                    time(&clients[slot].send_time);
                    
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                    int client_port = ntohs(client_addr.sin_port);
                    
                    format_timestamp(timestamp, sizeof(timestamp));
                    printf("[%s] %s:%d\n", timestamp, client_ip, client_port);
                    
                    struct sockaddr_in local_addr;
                    socklen_t local_addr_len = sizeof(local_addr);
                    char local_ip[INET_ADDRSTRLEN];
                    int local_port;
                    
                    if (getsockname(client_fd, (struct sockaddr*)&local_addr, &local_addr_len) == 0) {
                        inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, INET_ADDRSTRLEN);
                        local_port = ntohs(local_addr.sin_port);
                    } else {
                        strcpy(local_ip, "unknown");
                        local_port = 0;
                    }
                    
                    clients[slot].delay_us = 100000 + (rand() % 9900001);
                    
                    clients[slot].message_len = snprintf(clients[slot].message, BUFFER_SIZE,
                        "IP chiamante: %s\r\nPorta chiamante: %d\r\nIP locale: %s\r\nPorta locale: %d\r\nAttesa: %.1f s\r\n",
                        client_ip, client_port, local_ip, local_port, clients[slot].delay_us / 1000000.0);
                    num_clients++;
                } else {
                    close(client_fd);
                }
            }
        }
        
        // Handle client connections
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd == -1) continue;
            
            if (clients[i].state == STATE_SENDING) {
                int bytes = send(clients[i].fd, 
                               clients[i].message + clients[i].bytes_sent,
                               clients[i].message_len - clients[i].bytes_sent, 0);
                
                if (bytes > 0) {
                    clients[i].bytes_sent += bytes;
                    if (clients[i].bytes_sent >= clients[i].message_len) {
                        clients[i].state = STATE_WAITING;
                        
                        clients[i].timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
                        if (clients[i].timer_fd >= 0) {
                            struct itimerspec timer_spec;
                            timer_spec.it_value.tv_sec = clients[i].delay_us / 1000000;
                            timer_spec.it_value.tv_nsec = (clients[i].delay_us % 1000000) * 1000;
                            timer_spec.it_interval.tv_sec = 0;
                            timer_spec.it_interval.tv_nsec = 0;
                            timerfd_settime(clients[i].timer_fd, 0, &timer_spec, NULL);
                        } else {
                            clients[i].state = STATE_CLOSING;
                        }
                    }
                } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    clients[i].state = STATE_CLOSING;
                }
            }
            
            if (clients[i].state == STATE_WAITING && clients[i].timer_fd != -1) {
                for (int j = 1; j < nfds; j++) {
                    if (poll_fds[j].fd == clients[i].timer_fd && (poll_fds[j].revents & POLLIN)) {
                        uint64_t expirations;
                        read(clients[i].timer_fd, &expirations, sizeof(expirations));
                        
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clients[i].addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        int client_port = ntohs(clients[i].addr.sin_port);
                        
                        format_timestamp(timestamp, sizeof(timestamp));
                        printf("[%s] %s:%d timer expired (%.1f s)\n", timestamp, client_ip, client_port, 
                               clients[i].delay_us / 1000000.0);
                        
                        clients[i].state = STATE_CLOSING;
                        break;
                    }
                }
            }
            
            if (clients[i].state == STATE_CLOSING) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clients[i].addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                int client_port = ntohs(clients[i].addr.sin_port);
                
                format_timestamp(timestamp, sizeof(timestamp));
                printf("[%s] %s:%d closed\n", timestamp, client_ip, client_port);
                
                if (clients[i].timer_fd != -1) {
                    close(clients[i].timer_fd);
                    clients[i].timer_fd = -1;
                }
                close(clients[i].fd);
                clients[i].fd = -1;
                num_clients--;
            }
        }
    }

    close(server_socket);
    return 0;
}

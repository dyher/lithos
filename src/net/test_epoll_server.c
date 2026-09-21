#include "transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#define MAX_EVENTS 64
#define MAX_CLIENTS 1024

typedef struct {
    transport_t *trans;
} client_session_t;

client_session_t g_clients[MAX_CLIENTS];

int main() {
    printf("=== Lithos epoll + Transport Server ===\n");
    
    // 1. 創建 Server Transport
    transport_t *server = transport_create(TRANS_TYPE_TCP, 4096);
    if (transport_listen(server, 6900) < 0) {
        printf("❌ Failed to listen on port 6900\n");
        return 1;
    }
    int server_fd = transport_get_fd(server);
    printf("✅ Server listening on port 6900 (fd=%d)\n", server_fd);
    
    // 2. 初始化 epoll
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);
    
    printf("Waiting for connections...\n");
    
    // 3. Event Loop
    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1); // 阻塞等待
        
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            
            // --- 處理新連線 ---
            if (fd == server_fd) {
                transport_t *client = transport_create(TRANS_TYPE_TCP, 4096);
                if (transport_accept(client, server) == 0) {
                    int cfd = transport_get_fd(client);
                    printf("🟢 Client connected (fd=%d, ip=%s)\n", cfd, client->host);
                    
                    // 註冊到 epoll
                    ev.events = EPOLLIN;
                    ev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
                    
                    // 儲存 session
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (g_clients[j].trans == NULL) {
                            g_clients[j].trans = client;
                            break;
                        }
                    }
                } else {
                    transport_destroy(client);
                }
            } 
            // --- 處理客戶端數據 ---
            else {
                transport_t *client = NULL;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].trans && transport_get_fd(g_clients[j].trans) == fd) {
                        client = g_clients[j].trans;
                        break;
                    }
                }
                
                if (!client) continue;
                
                uint8_t buf[1024];
                ssize_t n_read = transport_read(client, buf, sizeof(buf));
                
                if (n_read <= 0) {
                    printf("🔴 Client disconnected (fd=%d)\n", fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    transport_close(client);
                    transport_destroy(client);
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (g_clients[j].trans == client) {
                            g_clients[j].trans = NULL;
                            break;
                        }
                    }
                } else {
                    // Echo: 原樣返回 (透過 Ring Buffer 寫入並 flush)
                    printf("📩 Received %zd bytes from fd=%d\n", n_read, fd);
                    transport_write(client, buf, n_read);
                    transport_flush(client);
                }
            }
        }
    }
    
    return 0;
}

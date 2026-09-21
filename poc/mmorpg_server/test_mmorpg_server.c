#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    #define INVALID_SOCK INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/epoll.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define INVALID_SOCK -1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_SESSIONS 1024
#define BUF_SIZE 4096

typedef struct {
    uint8_t data[BUF_SIZE];
    int head, tail;
} ring_buffer_t;

int rb_available(ring_buffer_t *rb) { return (rb->head - rb->tail + BUF_SIZE) % BUF_SIZE; }
int rb_read(ring_buffer_t *rb, uint8_t *out, int len) {
    if (rb_available(rb) < len) return 0;
    for (int i = 0; i < len; i++) { out[i] = rb->data[rb->tail]; rb->tail = (rb->tail + 1) % BUF_SIZE; }
    return len;
}
void rb_write(ring_buffer_t *rb, const uint8_t *in, int len) {
    for (int i = 0; i < len; i++) { rb->data[rb->head] = in[i]; rb->head = (rb->head + 1) % BUF_SIZE; }
}

typedef enum { STATE_WAIT_HEADER, STATE_WAIT_BODY } session_state_t;
typedef struct {
    socket_t fd;
    char ip[INET6_ADDRSTRLEN];
    ring_buffer_t in_buf;
    session_state_t state;
    uint16_t pkt_header;
    int pkt_len;
    int active;
} session_t;

session_t g_sessions[MAX_SESSIONS];

int get_ro_pkt_len(uint16_t header) {
    switch (header) {
        case 0x0064: return 55; case 0x0072: return 19; case 0x00A4: return 6; case 0x00F3: return 10;
        default: return 0;
    }
}

void handle_ro_packet(session_t *s, uint16_t header, int len, uint8_t *body) {
    printf("[%s] Received RO Packet: 0x%04X (Len: %d)\n", s->ip, header, len);
    if (header == 0x0064) printf("  -> Processing CA_LOGIN (Account Login)\n");
    else if (header == 0x00F3) printf("  -> Processing CZ_CHAT (Broadcast to AOI)\n");
}

void session_update(session_t *s) {
    switch (s->state) {
        case STATE_WAIT_HEADER:
            if (rb_available(&s->in_buf) >= 2) {
                uint8_t hdr[2]; rb_read(&s->in_buf, hdr, 2);
                s->pkt_header = (hdr[1] << 8) | hdr[0]; s->pkt_len = get_ro_pkt_len(s->pkt_header);
                if (s->pkt_len == 0) { printf("[%s] Unknown packet 0x%04X\n", s->ip, s->pkt_header); }
                else { s->pkt_len -= 2; s->state = STATE_WAIT_BODY; }
            } break;
        case STATE_WAIT_BODY:
            if (rb_available(&s->in_buf) >= s->pkt_len) {
                uint8_t body_buf[4096];
                // 關鍵修復：從 Ring Buffer 中讀取封包體，推進讀取指標 (tail)
                rb_read(&s->in_buf, body_buf, s->pkt_len);
                
                // 將封包體傳給業務層處理 (未來的 LPC / Fiber 掛載點)
                handle_ro_packet(s, s->pkt_header, s->pkt_len + 2, body_buf); 
                
                // 重置狀態，準備解析下一個封包
                s->state = STATE_WAIT_HEADER;
            } break;
    }
}

#ifdef _WIN32
void run_event_loop(socket_t listen_fd) { /* Windows select impl skipped for brevity in POC */ }
#else
void run_event_loop(socket_t listen_fd) {
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[64];
    ev.events = EPOLLIN; ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
    printf("Server started on port 6900 (Linux epoll mode - ARM64/x86_64)\n");
    while (1) {
        int n = epoll_wait(epfd, events, 64, 50);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                struct sockaddr_in addr; socklen_t len = sizeof(addr);
                int cfd = accept(listen_fd, (struct sockaddr*)&addr, &len);
                if (cfd >= 0) {
                    fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK);
                    for (int j = 0; j < MAX_SESSIONS; j++) {
                        if (!g_sessions[j].active) {
                            g_sessions[j].fd = cfd; g_sessions[j].active = 1; g_sessions[j].state = STATE_WAIT_HEADER;
                            inet_ntop(AF_INET, &addr.sin_addr, g_sessions[j].ip, sizeof(g_sessions[j].ip));
                            printf("Connected: %s\n", g_sessions[j].ip);
                            ev.events = EPOLLIN; ev.data.fd = cfd; epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev); break;
                        }
                    }
                }
            } else {
                int cfd = events[i].data.fd;
                for (int j = 0; j < MAX_SESSIONS; j++) {
                    if (g_sessions[j].active && g_sessions[j].fd == cfd) {
                        uint8_t buf[1024]; int r = recv(cfd, buf, sizeof(buf), 0);
                        if (r <= 0) {
                            printf("Disconnected: %s\n", g_sessions[j].ip);
                            epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL); CLOSE_SOCKET(cfd); g_sessions[j].active = 0;
                        } else { rb_write(&g_sessions[j].in_buf, buf, r); }
                        break;
                    }
                }
            }
        }
        for (int i = 0; i < MAX_SESSIONS; i++) { if (g_sessions[i].active) session_update(&g_sessions[i]); }
    }
}
#endif

int main() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(6900);
    int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)); listen(listen_fd, 10);
    printf("=== Lithos MMORPG Engine Prototype ===\nListening on port 6900...\n");
    run_event_loop(listen_fd);
    CLOSE_SOCKET(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

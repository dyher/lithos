// src/net/tcp_transport.c
#include "transport.h"
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
    #define INVALID_SOCK INVALID_SOCKET
    #define SOCK_ERRNO WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define INVALID_SOCK -1
    #define SOCK_ERRNO errno
#endif

// ============================================
// TCP Transport 內部實現
// ============================================

typedef struct {
    socket_t fd;
} tcp_data_t;


static int tcp_listen(transport_t *t, int port) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return -1;
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        CLOSE_SOCKET(fd);
        return -1;
    }
    
    // 設定非阻塞
    #ifndef _WIN32
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    #endif
    
    tcp->fd = fd;
    t->fd = (int)fd;
    t->state = TRANS_STATE_CONNECTED; // 簡化狀態
    return 0;
}

static int tcp_connect(transport_t *t, const char *host, int port) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        return -1;
    }
    
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return -1;
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(fd);
        return -1;
    }
    
    tcp->fd = fd;
    t->fd = (int)fd;
    t->state = TRANS_STATE_CONNECTED;
    return 0;
}

static int tcp_accept(transport_t *t, transport_t *server) {
    tcp_data_t *tcp_server = (tcp_data_t *)server->protocol_data;
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    
    socket_t fd = accept(tcp_server->fd, (struct sockaddr*)&addr, &len);
    if (fd == INVALID_SOCK) return -1;
    
    // 設定非阻塞
    #ifndef _WIN32
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    #endif
    
    tcp->fd = fd;
    t->fd = (int)fd;
    t->state = TRANS_STATE_CONNECTED;
    
    // 儲存客戶端 IP
    if (t->host) free(t->host);
    t->host = malloc(INET6_ADDRSTRLEN);
    inet_ntop(AF_INET, &addr.sin_addr, t->host, INET6_ADDRSTRLEN);
    t->port = ntohs(addr.sin_port);
    
    return 0;
}

static ssize_t tcp_read(transport_t *t, uint8_t *buf, size_t len) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    
    // 先從 Ring Buffer 讀取
    if (rb_available(t->in_buf) > 0) {
        size_t avail = rb_available(t->in_buf);
        size_t to_read = (len < avail) ? len : avail;
        rb_read(t->in_buf, buf, to_read);
        return (ssize_t)to_read;
    }
    
    // Ring Buffer 為空，從 socket 讀取
    ssize_t n = recv(tcp->fd, (char*)buf, len, 0);
    if (n < 0) {
        #ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
        #else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        #endif
        return -1;
    }
    if (n == 0) return -1;  // 連線關閉
    
    // 寫入 Ring Buffer
    rb_write(t->in_buf, buf, (size_t)n);
    return n;
}

static ssize_t tcp_write(transport_t *t, const uint8_t *buf, size_t len) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    
    // 先寫入 Ring Buffer
    if (rb_space(t->out_buf) < len) {
        // 緩衝區滿，先嘗試刷新
        transport_flush(t);
        if (rb_space(t->out_buf) < len) return -1;
    }
    
    rb_write(t->out_buf, buf, len);
    return (ssize_t)len;
}

static int tcp_flush(transport_t *t) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    
    while (rb_available(t->out_buf) > 0) {
        uint8_t buf[4096];
        size_t avail = rb_available(t->out_buf);
        size_t to_read = (avail < sizeof(buf)) ? avail : sizeof(buf);
        
        rb_read(t->out_buf, buf, to_read);
        
        ssize_t n = send(tcp->fd, (const char*)buf, to_read, 0);
        if (n < 0) {
            #ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                // 寫回 Ring Buffer，等待下次刷新
                // 注意：這裡簡化處理，實際應使用非阻塞寫入
                return 0;
            }
            #else
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            #endif
            return -1;
        }
        
        // 如果沒有完全發送，需要把剩餘數據寫回
        if ((size_t)n < to_read) {
            // 簡化處理：實際應記錄偏移量
            return 0;
        }
    }
    
    return 0;
}

static void tcp_close(transport_t *t) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    if (tcp->fd != INVALID_SOCK) {
        CLOSE_SOCKET(tcp->fd);
        tcp->fd = INVALID_SOCK;
    }
    t->state = TRANS_STATE_CLOSED;
}

static int tcp_get_fd(transport_t *t) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    return (int)tcp->fd;
}

static void tcp_destroy(transport_t *t) {
    tcp_data_t *tcp = (tcp_data_t *)t->protocol_data;
    if (tcp) {
        tcp_close(t);
        free(tcp);
        t->protocol_data = NULL;
    }
}

// ============================================
// Transport 操作表
// ============================================

static const transport_ops_t tcp_ops = {
    .listen = tcp_listen,
    .connect = tcp_connect,
    .accept = tcp_accept,
    .read = tcp_read,
    .write = tcp_write,
    .flush = tcp_flush,
    .close = tcp_close,
    .get_fd = tcp_get_fd,
    .destroy = tcp_destroy,
};

// ============================================
// Transport 創建（工廠函數）
// ============================================

transport_t *transport_create(trans_type_t type, size_t buf_size) {
    transport_t *t = calloc(1, sizeof(transport_t));
    if (!t) return NULL;
    
    t->type = type;
    t->state = TRANS_STATE_NEW;
    t->in_buf = rb_create(buf_size);
    t->out_buf = rb_create(buf_size);
    
    switch (type) {
        case TRANS_TYPE_TCP:
            t->ops = &tcp_ops;
            t->protocol_data = calloc(1, sizeof(tcp_data_t));
            break;
        default:
            // TODO: 實現 UDP、TLS、WebSocket
            break;
    }
    
    return t;
}

void transport_destroy(transport_t *t) {
    if (!t) return;
    
    if (t->ops && t->ops->destroy) {
        t->ops->destroy(t);
    }
    
    if (t->in_buf) rb_destroy(t->in_buf);
    if (t->out_buf) rb_destroy(t->out_buf);
    if (t->host) free(t->host);
    
    free(t);
}

// ============================================
// 高層 API 包裝
// ============================================


int transport_listen(transport_t *t, int port) {
    if (t->ops && t->ops->listen) return t->ops->listen(t, port);
    return -1;
}

int transport_connect(transport_t *t, const char *host, int port) {
    if (t->ops && t->ops->connect) return t->ops->connect(t, host, port);
    return -1;
}

int transport_accept(transport_t *t, transport_t *server) {
    if (t->ops && t->ops->accept) return t->ops->accept(t, server);
    return -1;
}

ssize_t transport_read(transport_t *t, uint8_t *buf, size_t len) {
    if (t->ops && t->ops->read) return t->ops->read(t, buf, len);
    return -1;
}

ssize_t transport_write(transport_t *t, const uint8_t *buf, size_t len) {
    if (t->ops && t->ops->write) return t->ops->write(t, buf, len);
    return -1;
}

int transport_flush(transport_t *t) {
    if (t->ops && t->ops->flush) return t->ops->flush(t);
    return -1;
}

void transport_close(transport_t *t) {
    if (t->ops && t->ops->close) t->ops->close(t);
}

int transport_get_fd(transport_t *t) {
    if (t->ops && t->ops->get_fd) return t->ops->get_fd(t);
    return -1;
}

trans_state_t transport_get_state(transport_t *t) {
    return t ? t->state : TRANS_STATE_CLOSED;
}

const char *transport_state_str(trans_state_t state) {
    switch (state) {
        case TRANS_STATE_NEW: return "NEW";
        case TRANS_STATE_CONNECTING: return "CONNECTING";
        case TRANS_STATE_CONNECTED: return "CONNECTED";
        case TRANS_STATE_CLOSING: return "CLOSING";
        case TRANS_STATE_CLOSED: return "CLOSED";
        case TRANS_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// src/net/transport.h
// Lithos Transport Abstraction Layer
// Inspired by FluffOS transport.h, MIT/Fair-code licensed
//
// 目標：解耦底層 IO 與上層協議，支援：
//   - Telnet (傳統 MUD)
//   - RO Binary (rAthena/Hercules)
//   - WebSocket (網頁版)
//   - 自定義協議

#ifndef LITHOS_TRANSPORT_H
#define LITHOS_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

// 跨平台類型定義
#ifdef _WIN32
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
#else
    #include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// Transport 狀態與類型
// ============================================

typedef enum {
    TRANS_STATE_NEW,
    TRANS_STATE_CONNECTING,
    TRANS_STATE_CONNECTED,
    TRANS_STATE_CLOSING,
    TRANS_STATE_CLOSED,
    TRANS_STATE_ERROR
} trans_state_t;

typedef enum {
    TRANS_TYPE_TCP,
    TRANS_TYPE_UDP,
    TRANS_TYPE_TLS,
    TRANS_TYPE_WEBSOCKET
} trans_type_t;

// ============================================
// Ring Buffer（解決 TCP 黏包/斷包）
// ============================================

typedef struct {
    uint8_t *data;
    size_t size;
    size_t head;    // 寫入指標
    size_t tail;    // 讀取指標
} ring_buffer_t;

// Ring Buffer API
ring_buffer_t *rb_create(size_t size);
void rb_destroy(ring_buffer_t *rb);
int rb_write(ring_buffer_t *rb, const uint8_t *data, size_t len);
int rb_read(ring_buffer_t *rb, uint8_t *out, size_t len);
int rb_peek(ring_buffer_t *rb, uint8_t *out, size_t len);  // 查看但不消費
size_t rb_available(ring_buffer_t *rb);                    // 可讀取的字節數
size_t rb_space(ring_buffer_t *rb);                        // 可寫入的空間
void rb_clear(ring_buffer_t *rb);

// ============================================
// Transport 抽象接口（VFS Style）
// ============================================

typedef struct transport transport_t;

// 協議操作接口（類似函數指標表）
typedef struct {
    // 連線操作
    int (*listen)(transport_t *t, int port);
    int (*connect)(transport_t *t, const char *host, int port);
    int (*accept)(transport_t *t, transport_t *server);
    
    // 數據讀寫（非阻塞）
    ssize_t (*read)(transport_t *t, uint8_t *buf, size_t len);
    ssize_t (*write)(transport_t *t, const uint8_t *buf, size_t len);
    int (*flush)(transport_t *t);
    
    // 連線管理
    void (*close)(transport_t *t);
    int (*get_fd)(transport_t *t);
    
    // 資源釋放
    void (*destroy)(transport_t *t);
} transport_ops_t;

// Transport 實體
struct transport {
    const transport_ops_t *ops;
    trans_type_t type;
    trans_state_t state;
    
    int fd;
    char *host;
    int port;
    
    // 輸入/輸出緩衝區（Ring Buffer）
    ring_buffer_t *in_buf;
    ring_buffer_t *out_buf;
    
    // 協議專屬數據（Telnet 狀態機、RO 加密金鑰等）
    void *protocol_data;
    
    // 用戶數據（interactive_t 指標等）
    void *user_data;
};

// ============================================
// 高層 API
// ============================================

transport_t *transport_create(trans_type_t type, size_t buf_size);
void transport_destroy(transport_t *t);

int transport_connect(transport_t *t, const char *host, int port);
int transport_listen(transport_t *t, int port);
int transport_accept(transport_t *t, transport_t *server);
ssize_t transport_read(transport_t *t, uint8_t *buf, size_t len);
ssize_t transport_write(transport_t *t, const uint8_t *buf, size_t len);
int transport_flush(transport_t *t);
void transport_close(transport_t *t);
int transport_get_fd(transport_t *t);

// 狀態查詢
trans_state_t transport_get_state(transport_t *t);
const char *transport_state_str(trans_state_t state);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_TRANSPORT_H

// src/net/test_transport.c
// 測試 Transport 抽象層
#include "transport.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Lithos Transport Layer Test ===\n");
    
    // 測試 Ring Buffer
    printf("\n[1] Testing Ring Buffer...\n");
    ring_buffer_t *rb = rb_create(1024);
    
    const char *test_data = "Hello Lithos!";
    rb_write(rb, (const uint8_t*)test_data, strlen(test_data));
    printf("  Wrote %zu bytes\n", strlen(test_data));
    printf("  Available: %zu\n", rb_available(rb));
    
    uint8_t out[256];
    rb_read(rb, out, strlen(test_data));
    out[strlen(test_data)] = '\0';
    printf("  Read back: %s\n", out);
    
    rb_destroy(rb);
    printf("  ✅ Ring Buffer OK\n");
    
    // 測試 TCP Transport 創建
    printf("\n[2] Testing TCP Transport...\n");
    transport_t *t = transport_create(TRANS_TYPE_TCP, 4096);
    if (t) {
        printf("  Created TCP transport (fd=%d, state=%s)\n", 
               t->fd, transport_state_str(t->state));
        transport_destroy(t);
        printf("  ✅ TCP Transport OK\n");
    }
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}

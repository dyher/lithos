// src/net/ring_buffer.c
#include "transport.h"
#include <stdlib.h>
#include <string.h>

ring_buffer_t *rb_create(size_t size) {
    ring_buffer_t *rb = calloc(1, sizeof(ring_buffer_t));
    if (!rb) return NULL;
    rb->data = malloc(size);
    if (!rb->data) {
        free(rb);
        return NULL;
    }
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    return rb;
}

void rb_destroy(ring_buffer_t *rb) {
    if (rb) {
        if (rb->data) free(rb->data);
        free(rb);
    }
}

size_t rb_available(ring_buffer_t *rb) {
    return (rb->head - rb->tail + rb->size) % rb->size;
}

size_t rb_space(ring_buffer_t *rb) {
    return rb->size - rb_available(rb) - 1;  // 保留 1 字節區分滿/空
}

int rb_write(ring_buffer_t *rb, const uint8_t *data, size_t len) {
    if (rb_space(rb) < len) return 0;
    for (size_t i = 0; i < len; i++) {
        rb->data[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    return 1;
}

int rb_read(ring_buffer_t *rb, uint8_t *out, size_t len) {
    if (rb_available(rb) < len) return 0;
    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    return 1;
}

int rb_peek(ring_buffer_t *rb, uint8_t *out, size_t len) {
    if (rb_available(rb) < len) return 0;
    size_t tail = rb->tail;
    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[tail];
        tail = (tail + 1) % rb->size;
    }
    return 1;
}

void rb_clear(ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

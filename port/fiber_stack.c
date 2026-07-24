#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "fiber_port.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#define GUARD_PAGE_SIZE 4096
int fiber_stack_alloc(fiber_ctx_t *ctx, size_t size) {
    if (ctx == NULL || size == 0) return 0;
    size_t total = size + GUARD_PAGE_SIZE;
#ifdef _WIN32
    void *mem = VirtualAlloc(NULL, total, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (mem == NULL) return 0;
    DWORD old;
    VirtualProtect(mem, GUARD_PAGE_SIZE, PAGE_NOACCESS, &old);
    ctx->stack_base = (uint8_t*)mem + GUARD_PAGE_SIZE;
#else
    void *mem = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return 0;
    mprotect(mem, GUARD_PAGE_SIZE, PROT_NONE);
    ctx->stack_base = (uint8_t*)mem + GUARD_PAGE_SIZE;
#endif
    ctx->stack_size = size;
    ctx->sp = NULL;
    return 1;
}
void fiber_stack_free(fiber_ctx_t *ctx) {
    if (ctx == NULL || ctx->stack_base == NULL) return;
    size_t total = ctx->stack_size + GUARD_PAGE_SIZE;
    void *mem = (uint8_t*)ctx->stack_base - GUARD_PAGE_SIZE;
#ifdef _WIN32
    VirtualFree(mem, 0, MEM_RELEASE);
#else
    munmap(mem, total);
#endif
    ctx->stack_base = NULL;
    ctx->sp = NULL;
    ctx->stack_size = 0;
}

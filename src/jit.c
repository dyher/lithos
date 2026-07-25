#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

/* === Internal State === */
static jit_code_buffer_t code_pool = {0};
static jit_template_t templates[JIT_TEMPLATE_MAX];
static int num_templates = 0;
static jit_function_t functions[JIT_MAX_FUNCTIONS];
static int num_functions = 0;
static int jit_is_enabled = 0;

/* === Code Pool Management === */

void jit_init(void) {
    if (jit_is_enabled) return;

#ifdef _WIN32
    code_pool.base = (uint8_t *)VirtualAlloc(NULL, JIT_CODE_POOL_SIZE,
                                              MEM_COMMIT | MEM_RESERVE,
                                              PAGE_EXECUTE_READWRITE);
#else
    code_pool.base = (uint8_t *)mmap(NULL, JIT_CODE_POOL_SIZE,
                                      PROT_READ | PROT_WRITE | PROT_EXEC,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code_pool.base == MAP_FAILED) {
        code_pool.base = NULL;
    }
#endif

    if (!code_pool.base) {
        fprintf(stderr, "JIT: failed to allocate code pool (%zu bytes)\n",
                (size_t)JIT_CODE_POOL_SIZE);
        jit_is_enabled = 0;
        return;
    }

    code_pool.capacity = JIT_CODE_POOL_SIZE;
    code_pool.offset = 0;

    memset(templates, 0, sizeof(templates));
    memset(functions, 0, sizeof(functions));
    num_templates = 0;
    num_functions = 0;
    jit_is_enabled = 1;

    printf("JIT: initialized, code pool %zu KB\n",
           (size_t)JIT_CODE_POOL_SIZE / 1024);
}

void jit_shutdown(void) {
    if (!jit_is_enabled) return;

    if (code_pool.base) {
#ifdef _WIN32
        VirtualFree(code_pool.base, 0, MEM_RELEASE);
#else
        munmap(code_pool.base, code_pool.capacity);
#endif
        code_pool.base = NULL;
    }

    jit_is_enabled = 0;
    num_templates = 0;
    num_functions = 0;
    printf("JIT: shutdown\n");
}

void *jit_alloc_code(size_t n) {
    if (!jit_is_enabled || !code_pool.base) return NULL;
    /* Align to 16 bytes */
    size_t aligned = (n + 15) & ~(size_t)15;
    if (code_pool.offset + aligned > code_pool.capacity) {
        fprintf(stderr, "JIT: code pool exhausted (used %zu / %zu)\n",
                code_pool.offset, code_pool.capacity);
        return NULL;
    }
    void *ptr = code_pool.base + code_pool.offset;
    code_pool.offset += aligned;
    return ptr;
}

size_t jit_code_used(void) { return code_pool.offset; }
size_t jit_code_capacity(void) { return code_pool.capacity; }

/* === Template Registry === */

void jit_register_template(uint8_t opcode, uint8_t *code, size_t size,
                           int stack_delta, int has_side_effect) {
    if (num_templates >= JIT_TEMPLATE_MAX) return;
    jit_template_t *t = &templates[num_templates++];
    t->opcode = opcode;
    t->code = code;
    t->code_size = size;
    t->stack_delta = stack_delta;
    t->has_side_effect = has_side_effect;
}

jit_template_t *jit_get_template(uint8_t opcode) {
    for (int i = 0; i < num_templates; i++) {
        if (templates[i].opcode == opcode) return &templates[i];
    }
    return NULL;
}

/* === Function Registry === */

int jit_register_function(void *entry, const char *prog_name,
                          int func_index, size_t code_size) {
    if (num_functions >= JIT_MAX_FUNCTIONS) return -1;
    jit_function_t *f = &functions[num_functions];
    f->entry = entry;
    f->prog_name = prog_name;
    f->func_index = func_index;
    f->call_count = 0;
    f->code_size = code_size;
    return num_functions++;
}

jit_function_t *jit_get_function(const char *prog_name, int func_index) {
    for (int i = 0; i < num_functions; i++) {
        if (functions[i].func_index == func_index &&
            strcmp(functions[i].prog_name, prog_name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

uint32_t jit_record_call(const char *prog_name, int func_index) {
    jit_function_t *f = jit_get_function(prog_name, func_index);
    if (f) return ++f->call_count;
    return 0;
}


/* === Hot Function Detection === */

typedef struct jit_hot_entry_s {
    const char *prog_name;
    int         func_addr;
    uint32_t    call_count;
} jit_hot_entry_t;

#define JIT_MAX_HOT_ENTRIES 4096
static jit_hot_entry_t hot_entries[JIT_MAX_HOT_ENTRIES];
static int num_hot_entries = 0;

static jit_hot_entry_t *find_hot_entry(const char *prog_name, int func_addr) {
    for (int i = 0; i < num_hot_entries; i++) {
        if (hot_entries[i].func_addr == func_addr &&
            strcmp(hot_entries[i].prog_name, prog_name) == 0) {
            return &hot_entries[i];
        }
    }
    return NULL;
}

uint32_t jit_record_function_entry(const char *prog_name, int func_addr) {
    if (!jit_is_enabled) return 0;
    jit_hot_entry_t *e = find_hot_entry(prog_name, func_addr);
    if (!e) {
        if (num_hot_entries >= JIT_MAX_HOT_ENTRIES) return 0;
        e = &hot_entries[num_hot_entries++];
        e->prog_name = prog_name;
        e->func_addr = func_addr;
        e->call_count = 0;
    }
    return ++e->call_count;
}

int jit_is_hot(const char *prog_name, int func_addr) {
    jit_hot_entry_t *e = find_hot_entry(prog_name, func_addr);
    return e && e->call_count >= JIT_HOT_THRESHOLD;
}

int jit_enabled(void) { return jit_is_enabled; }

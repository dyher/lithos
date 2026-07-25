
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_trace.h"
#include "jit_trace_compile.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trace recording state */
static trace_entry_t current_trace[TRACE_MAX_OPS];
static uint32_t trace_length = 0;
static int is_recording = 0;
static const char *trace_start_pc = NULL;

/* Compiled traces (simple linear list for now) */
#define MAX_TRACES 64
static jit_trace_t compiled_traces[MAX_TRACES];
static uint32_t num_traces = 0;
static uint32_t next_trace_id = 0;

/* Minimum trace length to compile (avoid trivial traces) */
#define TRACE_MIN_LENGTH 8


/* Hot loop tracking: map loop_pc → execution count */
#define MAX_HOT_LOOPS 128
typedef struct {
    const char *pc;
    uint32_t    count;
} hot_loop_entry_t;

static hot_loop_entry_t hot_loops[MAX_HOT_LOOPS];
static uint32_t num_hot_loops = 0;

int jit_trace_check_hot(const char *loop_pc) {
    if (!jit_enabled()) return 0;
    if (is_recording) return 0; /* already recording */

    /* Find or create entry */
    for (uint32_t i = 0; i < num_hot_loops; i++) {
        if (hot_loops[i].pc == loop_pc) {
            hot_loops[i].count++;
            if (hot_loops[i].count >= TRACE_HOT_THRESHOLD) {
                hot_loops[i].count = 0; /* reset after trigger */
                return 1;
            }
            return 0;
        }
    }
    /* New entry */
    if (num_hot_loops < MAX_HOT_LOOPS) {
        hot_loops[num_hot_loops].pc = loop_pc;
        hot_loops[num_hot_loops].count = 1;
        num_hot_loops++;
    }
    return 0;
}

void jit_trace_set_start_pc(const char *pc) {
    trace_start_pc = pc;
}

void jit_trace_init(void) {
    memset(compiled_traces, 0, sizeof(compiled_traces));
    printf("JIT: trace recorder initialized (max %d ops, min %d for compile)\n",
           TRACE_MAX_OPS, TRACE_MIN_LENGTH);
}

int jit_trace_start(void) {
    if (is_recording || !jit_enabled()) return -1;
    trace_length = 0;
    is_recording = 1;
    /* trace_start_pc will be set by caller */
    return (int)next_trace_id;
}

void jit_trace_record(uint8_t opcode, int16_t imm, uint8_t sp_type, uint8_t sp1_type) {
    if (!is_recording) return;
    if (trace_length >= TRACE_MAX_OPS) {
        jit_trace_abort();
        return;
    }
    trace_entry_t *e = &current_trace[trace_length++];
    e->opcode = opcode;
    e->flags = (imm != 0) ? 0x1 : 0;
    e->imm = imm;
    e->sp_type = sp_type;
    e->sp1_type = sp1_type;
}

void jit_trace_abort(void) {
    is_recording = 0;
    trace_length = 0;
    trace_start_pc = NULL;
}

int jit_trace_is_recording(void) {
    return is_recording;
}

/* Placeholder: actual native code emission for traces comes in J3b */
jit_trace_t *jit_trace_finish(void) {
    if (!is_recording) return NULL;
    is_recording = 0;

    if (trace_length < TRACE_MIN_LENGTH) {
        trace_length = 0;
        trace_start_pc = NULL;
        return NULL;
    }

    if (num_traces >= MAX_TRACES) {
        printf("JIT: trace pool full (%d traces), skipping\n", MAX_TRACES);
        trace_length = 0;
        trace_start_pc = NULL;
        return NULL;
    }

    jit_trace_t *t = &compiled_traces[num_traces];
    t->id = next_trace_id++;
    t->num_entries = trace_length;
    t->hit_count = 0;
    t->native_code = NULL; /* J3b: will be filled by trace compiler */
    t->code_size = 0;

    /* Copy entries */
    t->entries = (trace_entry_t *)malloc(trace_length * sizeof(trace_entry_t));
    if (!t->entries) {
        trace_length = 0;
        trace_start_pc = NULL;
        return NULL;
    }
    memcpy(t->entries, current_trace, trace_length * sizeof(trace_entry_t));

    /* Attempt compilation */
    jit_trace_compile(t);

    num_traces++;
    printf("JIT: recorded trace #%u (%u opcodes, start_pc=%p)\n",
           t->id, t->num_entries, (void *)trace_start_pc);

    trace_length = 0;
    trace_start_pc = NULL;
    return t;
}

jit_trace_t *jit_trace_lookup(const char *pc) {
    for (uint32_t i = 0; i < num_traces; i++) {
        /* J3b: match by start PC */
        (void)pc;
    }
    return NULL;
}

void jit_trace_stats(void) {
    printf("JIT: %u compiled traces\n", num_traces);
    for (uint32_t i = 0; i < num_traces; i++) {
        printf("  trace #%u: %u ops, %u hits, %zu bytes native\n",
               compiled_traces[i].id,
               compiled_traces[i].num_entries,
               compiled_traces[i].hit_count,
               compiled_traces[i].code_size);
    }
}

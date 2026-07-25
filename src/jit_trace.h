#ifndef LITHOS_JIT_TRACE_H
#define LITHOS_JIT_TRACE_H

#include <stdint.h>
#include <stddef.h>

/* Maximum trace length (opcodes) */
#define TRACE_MAX_OPS       256

/* Trace entry: one recorded opcode with type snapshot */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;      /* 0x1=has_imm, 0x2=branch_target */
    int16_t  imm;        /* immediate value or branch offset */
    uint8_t  sp_type;    /* type of top-of-stack at this point */
    uint8_t  sp1_type;   /* type of (sp-1) at this point */
} trace_entry_t;

/* A compiled trace */
typedef struct {
    void        *native_code;     /* pointer to emitted native code */
    size_t       code_size;
    trace_entry_t *entries;       /* recorded opcode sequence */
    uint32_t     num_entries;
    uint32_t     hit_count;       /* how many times this trace executed */
    uint32_t     id;
} jit_trace_t;

/* Initialize trace subsystem */
void jit_trace_init(void);

/* Start recording a new trace. Returns trace ID or -1 on failure. */
int jit_trace_start(void);

/* Record one opcode into the current trace. */
void jit_trace_record(uint8_t opcode, int16_t imm, uint8_t sp_type, uint8_t sp1_type);

/* Finish recording and attempt compilation.
 * Returns compiled trace pointer, or NULL if too short / compilation failed. */
jit_trace_t *jit_trace_finish(void);

/* Abort current trace recording (e.g., side effect encountered). */
void jit_trace_abort(void);

/* Check if currently recording a trace. */
int jit_trace_is_recording(void);

/* Look up compiled trace by starting PC. Returns NULL if not found. */
jit_trace_t *jit_trace_lookup(const char *pc);

/* Get trace stats for debugging. */
void jit_trace_stats(void);


/* Hot loop detection threshold (executions before attempting trace) */
#define TRACE_HOT_THRESHOLD   500

/* Record a backward branch execution. Returns 1 if trace should start. */
int jit_trace_check_hot(const char *loop_pc);

/* Set the start PC for current trace recording. */
void jit_trace_set_start_pc(const char *pc);

#endif /* LITHOS_JIT_TRACE_H */

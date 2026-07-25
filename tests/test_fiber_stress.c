
/* Fiber Stress Test - Phase 1: create/destroy only (no switch) */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "src/std.h"
#include "lib/lpc/fiber.h"
#include <stdio.h>
#include "src/jit.h"
#include "lib/lpc/types.h"

static int fiber_test_errors = 0;

static void dummy_entry(void *arg) {
    (void)arg;
    /* Never reached in phase 1 */
}

#define NUM_TEST_FIBERS 100

void run_fiber_stress_test(void) {
    printf("[FIBER_STRESS] Phase 1: create/destroy test\n"); fflush(stdout);
    fiber_subsystem_init();

    lpc_fiber_t *fibers[NUM_TEST_FIBERS];
    int created = 0;

    /* Create all fibers */
    for (int i = 0; i < NUM_TEST_FIBERS; i++) {
        fibers[i] = fiber_create(dummy_entry, NULL, 0);
        if (fibers[i]) created++;
        else { printf("[FIBER_STRESS] ERROR: create failed at %d\n", i); fflush(stdout); fiber_test_errors++; }
    }
    printf("[FIBER_STRESS] Created %d/%d fibers, count=%d\n", created, NUM_TEST_FIBERS, fiber_count()); fflush(stdout);

    /* Destroy all fibers WITHOUT resuming */
    for (int i = 0; i < NUM_TEST_FIBERS; i++) {
        if (fibers[i]) fiber_destroy(fibers[i]);
    }
    printf("[FIBER_STRESS] Destroyed all, count=%d\n", fiber_count()); fflush(stdout);

    if (fiber_test_errors == 0 && fiber_count() == 0) {
        printf("[FIBER_STRESS] Phase 1 PASS\n"); fflush(stdout);
    } else {
        printf("[FIBER_STRESS] Phase 1 FAIL errors=%d count=%d\n", fiber_test_errors, fiber_count()); fflush(stdout);
    }

    /* Phase 2: single fiber create + resume + destroy */
    printf("[FIBER_STRESS] Phase 2: single fiber resume test\n"); fflush(stdout);
    lpc_fiber_t *single = fiber_create(dummy_entry, NULL, 0);
    if (single) {
        printf("[FIBER_STRESS] Single fiber created, attempting resume...\n"); fflush(stdout);
        fiber_resume(single);
        printf("[FIBER_STRESS] Resume returned safely\n"); fflush(stdout);
        fiber_destroy(single);
        printf("[FIBER_STRESS] Phase 2 PASS\n"); fflush(stdout);
    } else {
        printf("[FIBER_STRESS] Phase 2 FAIL: could not create single fiber\n"); fflush(stdout);
    }


    /* Phase 3: JIT native template correctness */
    printf("[JIT_TEST] Phase 3: native CONST0/CONST1 verification\n"); fflush(stdout);
    if (jit_enabled()) {
        jit_template_t *t0 = jit_get_template(15); /* F_CONST0 */
        jit_template_t *t1 = jit_get_template(16); /* F_CONST1 */
        if (t0 && t0->code_size > sizeof(void*) && t1 && t1->code_size > sizeof(void*)) {
            /* Save sp state */
            extern svalue_t *sp;
            svalue_t *saved_sp = sp;

            /* Execute native F_CONST0 */
            typedef void (*native_fn)(void);
            union { void *obj; native_fn fn; } cast0, cast1;
            cast0.obj = (void *)t0->code;
            cast1.obj = (void *)t1->code;

            cast0.fn(); /* should push T_NUMBER 0 */
            if (sp == saved_sp + 1 && sp->type == 2 && sp->u.number == 0) {
                printf("[JIT_TEST] F_CONST0 native: PASS\n"); fflush(stdout);
            } else {
                printf("[JIT_TEST] F_CONST0 native: FAIL (sp=%p type=%d num=%lld)\n",
                       (void*)sp, sp->type, (long long)sp->u.number); fflush(stdout);
                fiber_test_errors++;
            }

            cast1.fn(); /* should push T_NUMBER 1 */
            if (sp == saved_sp + 2 && sp->type == 2 && sp->u.number == 1) {
                printf("[JIT_TEST] F_CONST1 native: PASS\n"); fflush(stdout);
            } else {
                printf("[JIT_TEST] F_CONST1 native: FAIL (sp=%p type=%d num=%lld)\n",
                       (void*)sp, sp->type, (long long)sp->u.number); fflush(stdout);
                fiber_test_errors++;
            }

            /* Restore sp */
            sp = saved_sp;
        } else {
            printf("[JIT_TEST] SKIP: native templates not available\n"); fflush(stdout);
        }
    } else {
        printf("[JIT_TEST] SKIP: JIT disabled\n"); fflush(stdout);
    }

    printf("[FIBER_STRESS] === ALL PHASES COMPLETE ===\n"); fflush(stdout);
}


/* Fiber Stress Test - Phase 1: create/destroy only (no switch) */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "src/std.h"
#include "lib/lpc/fiber.h"
#include <stdio.h>

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

    printf("[FIBER_STRESS] === ALL PHASES COMPLETE ===\n"); fflush(stdout);
}

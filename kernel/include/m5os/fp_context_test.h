#pragma once
#include <m5os/types.h>
typedef struct {
    uint32_t iterations[2];
    uint32_t failures[2];
    uint32_t preempted_loops[2];
    uint32_t loop_ticks[2];
    uint32_t ticks;
    bool complete;
} fp_context_test_result_t;
/* Bounded two-task test; call from task context with preemption running. */
bool fp_context_test_run(fp_context_test_result_t *out);

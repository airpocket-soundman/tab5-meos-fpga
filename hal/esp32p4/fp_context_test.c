/* On-device ilp32f isolation test. No floating point operations in ISRs. */
#include <m5os/arch.h>
#include <m5os/fp_context_test.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <string.h>
static volatile bool test_busy;
static struct {
    volatile uint32_t iterations, failures, preempted_loops, loop_ticks;
    volatile bool done;
    uint32_t seed;
} workers[2];
static void fp_worker(void *arg) {
    unsigned n = (unsigned)(uintptr_t)arg;
    uint32_t saved;
    __asm__ volatile("frcsr %0" : "=r"(saved));
    for (unsigned k = 0; k < 64; k++) {
        uint32_t actual, csr;
        uint32_t expected = workers[n].seed + k;
        uint32_t wanted_csr = (n + 1u) << 5; /* distinct legal rounding modes, no sticky flags */
        __asm__ volatile("fscsr %0" ::"r"(wanted_csr));
        /* All ABI callee FP registers survive a yield; every call-clobbered
         * integer and FP register is declared to the compiler. */
        __asm__ volatile("addi t1, %1, 0\n\t"
                         "fmv.w.x fs0, t1\n\t"
                         "addi t1, %1, 1\n\t"
                         "fmv.w.x fs1, t1\n\t"
                         "addi t1, %1, 2\n\t"
                         "fmv.w.x fs2, t1\n\t"
                         "addi t1, %1, 3\n\t"
                         "fmv.w.x fs3, t1\n\t"
                         "addi t1, %1, 4\n\t"
                         "fmv.w.x fs4, t1\n\t"
                         "addi t1, %1, 5\n\t"
                         "fmv.w.x fs5, t1\n\t"
                         "addi t1, %1, 6\n\t"
                         "fmv.w.x fs6, t1\n\t"
                         "addi t1, %1, 7\n\t"
                         "fmv.w.x fs7, t1\n\t"
                         "addi t1, %1, 8\n\t"
                         "fmv.w.x fs8, t1\n\t"
                         "addi t1, %1, 9\n\t"
                         "fmv.w.x fs9, t1\n\t"
                         "addi t1, %1, 10\n\t"
                         "fmv.w.x fs10, t1\n\t"
                         "addi t1, %1, 11\n\t"
                         "fmv.w.x fs11, t1\n\t"
                         "call task_yield\n\t"
                         "li %0, 0\n\t"
                         "addi t1, %1, 0\n\t"
                         "fmv.x.w t2, fs0\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 1\n\t"
                         "fmv.x.w t2, fs1\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 2\n\t"
                         "fmv.x.w t2, fs2\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 3\n\t"
                         "fmv.x.w t2, fs3\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 4\n\t"
                         "fmv.x.w t2, fs4\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 5\n\t"
                         "fmv.x.w t2, fs5\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 6\n\t"
                         "fmv.x.w t2, fs6\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 7\n\t"
                         "fmv.x.w t2, fs7\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 8\n\t"
                         "fmv.x.w t2, fs8\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 9\n\t"
                         "fmv.x.w t2, fs9\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 10\n\t"
                         "fmv.x.w t2, fs10\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 11\n\t"
                         "fmv.x.w t2, fs11\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         : "=&r"(actual)
                         : "r"(expected)
                         : "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a0", "a1", "a2", "a3",
                           "a4", "a5", "a6", "a7", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
                           "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17",
                           "f18", "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27",
                           "f28", "f29", "f30", "f31", "memory");
        __asm__ volatile("frcsr %0" : "=r"(csr));
        if (actual || csr != wanted_csr)
            workers[n].failures++;
        uint32_t tick_before = sched_ticks();
        /* All32 FP registers stay live without calls across timer preemption. */
        __asm__ volatile("addi t1, %1, 0\n\t"
                         "fmv.w.x f0, t1\n\t"
                         "addi t1, %1, 1\n\t"
                         "fmv.w.x f1, t1\n\t"
                         "addi t1, %1, 2\n\t"
                         "fmv.w.x f2, t1\n\t"
                         "addi t1, %1, 3\n\t"
                         "fmv.w.x f3, t1\n\t"
                         "addi t1, %1, 4\n\t"
                         "fmv.w.x f4, t1\n\t"
                         "addi t1, %1, 5\n\t"
                         "fmv.w.x f5, t1\n\t"
                         "addi t1, %1, 6\n\t"
                         "fmv.w.x f6, t1\n\t"
                         "addi t1, %1, 7\n\t"
                         "fmv.w.x f7, t1\n\t"
                         "addi t1, %1, 8\n\t"
                         "fmv.w.x f8, t1\n\t"
                         "addi t1, %1, 9\n\t"
                         "fmv.w.x f9, t1\n\t"
                         "addi t1, %1, 10\n\t"
                         "fmv.w.x f10, t1\n\t"
                         "addi t1, %1, 11\n\t"
                         "fmv.w.x f11, t1\n\t"
                         "addi t1, %1, 12\n\t"
                         "fmv.w.x f12, t1\n\t"
                         "addi t1, %1, 13\n\t"
                         "fmv.w.x f13, t1\n\t"
                         "addi t1, %1, 14\n\t"
                         "fmv.w.x f14, t1\n\t"
                         "addi t1, %1, 15\n\t"
                         "fmv.w.x f15, t1\n\t"
                         "addi t1, %1, 16\n\t"
                         "fmv.w.x f16, t1\n\t"
                         "addi t1, %1, 17\n\t"
                         "fmv.w.x f17, t1\n\t"
                         "addi t1, %1, 18\n\t"
                         "fmv.w.x f18, t1\n\t"
                         "addi t1, %1, 19\n\t"
                         "fmv.w.x f19, t1\n\t"
                         "addi t1, %1, 20\n\t"
                         "fmv.w.x f20, t1\n\t"
                         "addi t1, %1, 21\n\t"
                         "fmv.w.x f21, t1\n\t"
                         "addi t1, %1, 22\n\t"
                         "fmv.w.x f22, t1\n\t"
                         "addi t1, %1, 23\n\t"
                         "fmv.w.x f23, t1\n\t"
                         "addi t1, %1, 24\n\t"
                         "fmv.w.x f24, t1\n\t"
                         "addi t1, %1, 25\n\t"
                         "fmv.w.x f25, t1\n\t"
                         "addi t1, %1, 26\n\t"
                         "fmv.w.x f26, t1\n\t"
                         "addi t1, %1, 27\n\t"
                         "fmv.w.x f27, t1\n\t"
                         "addi t1, %1, 28\n\t"
                         "fmv.w.x f28, t1\n\t"
                         "addi t1, %1, 29\n\t"
                         "fmv.w.x f29, t1\n\t"
                         "addi t1, %1, 30\n\t"
                         "fmv.w.x f30, t1\n\t"
                         "addi t1, %1, 31\n\t"
                         "fmv.w.x f31, t1\n\t"
                         "li t0, 4000000\n\t"
                         "1:\n\t"
                         "addi t0,t0,-1\n\t"
                         "bnez t0,1b\n\t"
                         "li %0, 0\n\t"
                         "addi t1, %1, 0\n\t"
                         "fmv.x.w t2, f0\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 1\n\t"
                         "fmv.x.w t2, f1\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 2\n\t"
                         "fmv.x.w t2, f2\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 3\n\t"
                         "fmv.x.w t2, f3\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 4\n\t"
                         "fmv.x.w t2, f4\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 5\n\t"
                         "fmv.x.w t2, f5\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 6\n\t"
                         "fmv.x.w t2, f6\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 7\n\t"
                         "fmv.x.w t2, f7\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 8\n\t"
                         "fmv.x.w t2, f8\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 9\n\t"
                         "fmv.x.w t2, f9\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 10\n\t"
                         "fmv.x.w t2, f10\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 11\n\t"
                         "fmv.x.w t2, f11\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 12\n\t"
                         "fmv.x.w t2, f12\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 13\n\t"
                         "fmv.x.w t2, f13\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 14\n\t"
                         "fmv.x.w t2, f14\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 15\n\t"
                         "fmv.x.w t2, f15\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 16\n\t"
                         "fmv.x.w t2, f16\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 17\n\t"
                         "fmv.x.w t2, f17\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 18\n\t"
                         "fmv.x.w t2, f18\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 19\n\t"
                         "fmv.x.w t2, f19\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 20\n\t"
                         "fmv.x.w t2, f20\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 21\n\t"
                         "fmv.x.w t2, f21\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 22\n\t"
                         "fmv.x.w t2, f22\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 23\n\t"
                         "fmv.x.w t2, f23\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 24\n\t"
                         "fmv.x.w t2, f24\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 25\n\t"
                         "fmv.x.w t2, f25\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 26\n\t"
                         "fmv.x.w t2, f26\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 27\n\t"
                         "fmv.x.w t2, f27\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 28\n\t"
                         "fmv.x.w t2, f28\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 29\n\t"
                         "fmv.x.w t2, f29\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 30\n\t"
                         "fmv.x.w t2, f30\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         "addi t1, %1, 31\n\t"
                         "fmv.x.w t2, f31\n\t"
                         "beq t1,t2,2f\n\t"
                         "addi %0,%0,1\n\t"
                         "2:\n\t"
                         : "=&r"(actual)
                         : "r"(expected)
                         : "t0", "t1", "t2", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8",
                           "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18",
                           "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27", "f28",
                           "f29", "f30", "f31", "memory");
        __asm__ volatile("frcsr %0" : "=r"(csr));
        uint32_t elapsed = sched_ticks() - tick_before;
        if (elapsed)
            workers[n].preempted_loops++;
        workers[n].loop_ticks += elapsed;
        if (actual || csr != wanted_csr)
            workers[n].failures++;
        workers[n].iterations++;
    }
    __asm__ volatile("fscsr %0" ::"r"(saved));
    workers[n].done = true;
}
bool fp_context_test_run(fp_context_test_result_t *out) {
    if (!out || !sched_started() || !arch_irq_enabled())
        return false;
    uint32_t irq = arch_irq_disable();
    if (test_busy) {
        arch_irq_restore(irq);
        return false;
    }
    test_busy = true;
    arch_irq_restore(irq);
    memset(workers, 0, sizeof(workers));
    memset(out, 0, sizeof(*out));
    workers[0].seed = 0x3f010000;
    workers[1].seed = 0x40020000;
    uint32_t start_ticks = sched_ticks();
    int first = task_create("fp-test-a", fp_worker, (void *)(uintptr_t)0, TASK_PRIO_DEFAULT);
    int second = first > 0
                     ? task_create("fp-test-b", fp_worker, (void *)(uintptr_t)1, TASK_PRIO_DEFAULT)
                     : -1;
    uint64_t deadline = hal_time_now_us() + 10000000ULL;
    if (second > 0)
        while ((!workers[0].done || !workers[1].done) && hal_time_now_us() < deadline)
            task_sleep(10);
    out->complete = first > 0 && second > 0 && workers[0].done && workers[1].done;
    if (!out->complete) {
        if (first > 0)
            task_kill((uint32_t)first);
        if (second > 0)
            task_kill((uint32_t)second);
    }
    for (unsigned i = 0; i < 2; i++) {
        out->iterations[i] = workers[i].iterations;
        out->failures[i] = workers[i].failures;
        out->preempted_loops[i] = workers[i].preempted_loops;
        out->loop_ticks[i] = workers[i].loop_ticks;
    }
    out->ticks = sched_ticks() - start_ticks;
    test_busy = false;
    return out->complete && out->ticks > 0 && out->preempted_loops[0] > 0 &&
           out->preempted_loops[1] > 0 && out->failures[0] == 0 && out->failures[1] == 0;
}

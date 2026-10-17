/*
 * Cooperative context switch for Xtensa LX7, windowed ABI (CoreS3 / ESP32-S3).
 *
 * Facts this relies on (checked in ESP-IDF v4.4.3:
 * components/xtensa/include/xtensa/coreasm.h and
 * components/freertos/port/xtensa/xtensa_vectors.S, _WindowUnderflow4):
 *
 *  - The register file is a 64-entry circular buffer seen through a 16-register
 *    window.  CALLn rotates the window by n registers, ENTRY allocates the
 *    callee's stack frame, RETW rotates back.
 *  - A caller's a0..a3 live in the "base save area" = the 16 bytes just below
 *    the callee's sp.  Registers a4..a15 of a caller that has been spilled live
 *    in its own frame.
 *  - If RETW rotates into a window whose WINDOWSTART bit is clear (i.e. that
 *    window has been spilled), the CPU takes a Window Underflow exception;
 *    the handler reloads a0..a3 (and more for call8/12) from memory using the
 *    callee's sp (a5 in the handler) and resumes.
 *
 * Therefore a context switch does NOT have to copy 64 registers.  It must:
 *   1. spill every *other* live window to its stack (window_spill4),
 *   2. remember this function's own a0 (return address + window increment)
 *      and sp,
 *   3. exchange sp,
 *   4. RETW: the window underflow handler faults in the caller's registers
 *      from the NEW task's stack.
 * Everything a task needs is on its own stack; the only thing the TCB stores
 * is one pointer.  (Phase 3, interrupt-driven switching, additionally has to
 * save PS/PC/SAR etc.; here the switch is a plain function call so the C ABI
 * has already told the compiler which registers are dead.)
 */

#include <xtensa/coreasm.h>

#define CTX_FRAME_SIZE   32      /* locals(16) + base save area for caller (16) */
#define CTX_RET_OFFSET    0      /* [sp+0] holds the saved a0 */

    .section .iram1, "ax"
    .align  4
    .global arch_context_switch
    .type   arch_context_switch, @function

/* void arch_context_switch(uint32_t **old_sp [a2], uint32_t *new_sp [a3]) */
arch_context_switch:
    entry   a1, CTX_FRAME_SIZE          /* new frame; window rotated by caller's CALLn */

    /* Step 1: push every caller window out to memory.  Clobbers a4..a15 but
     * preserves a0..a3, which we still need. */
    window_spill4

    /* Step 2: our own return address (a0: bits 31:30 = window increment, rest =
     * PC) is the one thing the spill did not store.  Keep it in our frame. */
    s32i    a0, a1, CTX_RET_OFFSET
    s32i    a1, a2, 0                   /* *old_sp = sp : "this task is parked here" */

    /* Step 3: switch stacks. From here on the old task's stack is untouched. */
    mov     a1, a3

    /* Step 4: pick up the new task's saved a0 and return into it.  RETW
     * rotates the window back; that window is not live, so the underflow
     * handler reloads a0..a3 from [new_sp-16 .. new_sp-4] and the rest of the
     * caller's window from the stack, and execution continues right after
     * the arch_context_switch() call that parked the new task. */
    l32i    a0, a1, CTX_RET_OFFSET
    retw

    .size   arch_context_switch, . - arch_context_switch

    window_spill_common                 /* helper code window_spill4 calls into */

/* ------------------------------------------------------------------------- */
/*
 * First entry of a brand-new task.
 *
 * arch_task_stack_init() fabricates a stack that looks exactly like one parked
 * by arch_context_switch(), except the saved "caller" registers are:
 *     a0 = 0 (ends backtraces), a1 = top of stack, a2 = arg, a3 = entry
 * and the saved return address points here.  So the first RETW lands here
 * with a window holding those values and a valid stack pointer.
 *
 * This is not a normal function (no ENTRY): it plays the role of the
 * "caller" of entry(arg), then of task_exit().
 */
    .align  4
    .global arch_task_trampoline
    .type   arch_task_trampoline, @function
arch_task_trampoline:
    rsil    a5, 0                       /* a task is always switched to with interrupts masked
                                           (scheduler holds INTLEVEL 3); a brand-new task has
                                           no code that would unmask them, so do it here */
    mov     a6, a2                      /* CALL4 passes args in the callee's a2 = our a6 */
    callx4  a3                          /* entry(arg) */
    call4   task_exit                   /* entry returned: terminate the task; never returns */
    .size   arch_task_trampoline, . - arch_task_trampoline

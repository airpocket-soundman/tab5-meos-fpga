/*
 * M5OS vector table (ESP32-S3, Xtensa LX7) and level-3 interrupt entry.
 *
 * WHY A SEPARATE TABLE?
 *   ESP-IDF's vectors live in freertos/port/xtensa/xtensa_vectors.S.  Its
 *   medium-priority interrupt path (_xt_medint2..3) calls XT_RTOS_INT_ENTER /
 *   XT_RTOS_INT_EXIT = _frxt_int_enter/_frxt_int_exit, which manipulate
 *   FreeRTOS's pxCurrentTCB.  We cannot use them.  Instead of forking that
 *   1900-line file, we point VECBASE (the relocatable vector base register,
 *   XCHAL_HAVE_VECBASE=1; IDF itself writes it in cpu_start.c) at THIS table.
 *   Every slot is a plain "j <idf handler>", so window over/underflow and all
 *   exceptions keep working exactly as before.  Only level 3 is ours.
 *
 *   A slot may not use any register (window vectors run with all registers
 *   live), so slots are pure PC-relative jumps.  no-transform guarantees the
 *   assembler never rewrites them into a multi-instruction sequence.
 *
 * Offsets are fixed by the core: xtensa/config/core-isa.h XCHAL_*_VECOFS.
 * VECBASE's low 10 bits are reserved, so the table is 1 KiB aligned.
 *
 * WHY LEVEL 3 / CCOMPARE1?
 *   core-isa.h: XCHAL_INT15 = TIMER1 (CCOMPARE1), level 3; EXCM_LEVEL = 3, so
 *   level 3 is the highest "medium" level: PS.EXCM gates it, and C code
 *   (windowed ABI) may run in its handler once EXCM is cleared.  CCOMPARE0 is
 *   level 1 (shares the user exception vector); CCOMPARE2 is level 5 and IDF
 *   uses level 4/5 for cache-error and watchdog.
 */
#include <xtensa/coreasm.h>
#include <xtensa/corebits.h>

/* Provokes a window overflow for every live window except the current one,
 * without needing any scratch register (same technique and register sequence
 * as ESP-IDF's SPILL_ALL_WINDOWS in freertos/port/xtensa/xt_asm_utils.h,
 * for XCHAL_NUM_AREGS == 64): touching a12 of each rotated view hits the
 * registers of any still-live older frame and traps into its overflow vector. */
    .macro M5_SPILL_ALL_WINDOWS
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    4
    .endm

/* Interrupt frame, saved on the interrupted task's own stack. Word offsets *4. */
#define F_PC        0
#define F_PS        4
#define F_SAR       8
#define F_LBEG      12
#define F_LEND      16
#define F_LCOUNT    20
#define F_THREADPTR 24
#define F_ACCLO     28
#define F_ACCHI     32
#define F_A0        36              /* a<n> at F_A0 + 4*n, n = 0..15 */
/*
 * Frame size = 112 bytes of saved state + 16 reserved bytes at the TOP.
 * Windowed-ABI rule: the 16 bytes directly below a function's sp are the
 * "base save area" where a window spill stores that function's CALLER's
 * a0..a3.  The interrupted code's sp is exactly the top of our frame, so when
 * the scheduler later spills windows (arch_context_switch), those 16 bytes
 * get overwritten.  State must therefore live in [sp, sp+F_SIZE-16).
 * (IDF's XT_STK_FRMSZ reserves the same extra space for the same reason.)
 * The state itself is 36 + 64 = 100 bytes, rounded up to 112.
 */
#define F_SIZE      128

#define PS_C_RUN    (PS_INTLEVEL(3) | PS_UM | PS_WOE)         /* EXCM clear: C may run  */
#define PS_C_EXIT   (PS_INTLEVEL(3) | PS_UM | PS_WOE | PS_EXCM)/* EXCM set: safe to rfi  */

    .section .iram1, "ax"
    .align  1024
    .global m5_vector_table
m5_vector_table:
    .begin  no-transform
    .org    0x000
    j       _WindowOverflow4
    .org    0x040
    j       _WindowUnderflow4
    .org    0x080
    j       _WindowOverflow8
    .org    0x0C0
    j       _WindowUnderflow8
    .org    0x100
    j       _WindowOverflow12
    .org    0x140
    j       _WindowUnderflow12
    .org    0x180
    j       _Level2Vector
    .org    0x1C0                   /* ---- level 3: M5OS ---- */
    wsr     a0, EXCSAVE_3           /* a0 is the only free-able register: park it */
    j       m5_level3_entry
    .org    0x200
    j       _Level4Vector
    .org    0x240
    j       _Level5Vector
    .org    0x280
    j       _DebugExceptionVector   /* level 6 */
    .org    0x2C0
    j       _NMIExceptionVector
    .org    0x300
    j       _KernelExceptionVector
    .org    0x340
    j       _UserExceptionVector
    .org    0x3C0
    j       _DoubleExceptionVector
    .org    0x400
    .end    no-transform

/* ------------------------------------------------------------------------- */
/*
 * Level-3 interrupt: save everything, run C with interrupts masked, restore.
 *
 * On entry (hardware): PC -> EPC3, PS -> EPS3, PS.EXCM=1, INTLEVEL=3.
 *   a0 parked in EXCSAVE_3, every other register still the interruptee's.
 *   sp (a1) is the interrupted task's stack pointer.
 *
 * The frame is built on the interrupted task's stack.  If the scheduler then
 * switches tasks inside sched_tick(), this whole handler is simply "parked"
 * in arch_context_switch() on that stack, like any other task; it continues
 * (restoring this frame and returning to the interrupted code) when the task
 * is next scheduled.  That is why preemptive and cooperative switches share
 * one context-switch routine.
 */
    .align  4
    .global m5_level3_entry
    .type   m5_level3_entry, @function
m5_level3_entry:
    /*
     * STEP 1: save the interruptee's registers WITHOUT touching a1.
     *   a0 is free to use as the frame pointer (the real a0 is in EXCSAVE_3).
     *   The frame is addressed as a1 - F_SIZE but a1 itself stays equal to the
     *   interrupted function's sp until step 2 is done.  See the long note
     *   below for why that matters.
     */
    addi    a0, a1, -F_SIZE
    s32i    a1,  a0, F_A0 + 4*1             /* interrupted sp */
    s32i    a2,  a0, F_A0 + 4*2
    s32i    a3,  a0, F_A0 + 4*3
    s32i    a4,  a0, F_A0 + 4*4
    s32i    a5,  a0, F_A0 + 4*5
    s32i    a6,  a0, F_A0 + 4*6
    s32i    a7,  a0, F_A0 + 4*7
    s32i    a8,  a0, F_A0 + 4*8
    s32i    a9,  a0, F_A0 + 4*9
    s32i    a10, a0, F_A0 + 4*10
    s32i    a11, a0, F_A0 + 4*11
    s32i    a12, a0, F_A0 + 4*12
    s32i    a13, a0, F_A0 + 4*13
    s32i    a14, a0, F_A0 + 4*14
    s32i    a15, a0, F_A0 + 4*15
    rsr     a2, EXCSAVE_3
    s32i    a2, a0, F_A0                    /* interruptee's a0 */

    rsr     a2, EPC_3
    s32i    a2, a0, F_PC
    rsr     a2, EPS_3
    s32i    a2, a0, F_PS
    rsr     a2, SAR
    s32i    a2, a0, F_SAR
    rsr     a2, LBEG
    s32i    a2, a0, F_LBEG
    rsr     a2, LEND
    s32i    a2, a0, F_LEND
    rsr     a2, LCOUNT
    s32i    a2, a0, F_LCOUNT
    movi    a2, 0
    wsr     a2, LCOUNT              /* a live zero-overhead loop must not fire inside C */
    rur.threadptr a2
    s32i    a2, a0, F_THREADPTR
    rsr     a2, ACCLO
    s32i    a2, a0, F_ACCLO
    rsr     a2, ACCHI
    s32i    a2, a0, F_ACCHI

    /*
     * STEP 2: spill every live register window of the interrupted code to
     * memory, while a1 is still the interrupted function's sp.
     *
     * WHY NOW, AND WITH THE OLD sp: the interrupted function's CALLER's
     * a0..a3 must end up in [sp_interrupted-16, sp_interrupted) - the place
     * the Window Underflow handler will read them from when that function
     * eventually does RETW.  Spilling later (e.g. inside the scheduler's
     * arch_context_switch) would use our *lowered* sp and store them 128
     * bytes too low; the interrupted code would then reload garbage as its
     * caller's a1/a0 and crash on return.  (First observed on hardware.)
     * ESP-IDF's _xt_context_save does the same (SPILL_ALL_WINDOWS before
     * allocating its frame).
     *
     * SPILL_ALL_WINDOWS provokes window-overflow exceptions, which need
     * PS.EXCM = 0 and PS.WOE = 1, and they clobber EPC1.  Only a0, a2, a3
     * are used as scratch (everything is already saved above).
     */
    rsr     a2, PS                  /* restored after the spill */
    movi    a3, PS_INTLEVEL(3) | PS_UM | PS_WOE
    wsr     a3, PS
    rsync
    rsr     a0, EPC1                /* window overflow exceptions overwrite EPC1 */

    M5_SPILL_ALL_WINDOWS

    wsr     a2, PS
    rsync
    wsr     a0, EPC1

    /* STEP 3: now it is safe to move sp down and run on the frame. */
    addi    sp, sp, -F_SIZE

    /* Leave exception mode: windowed C code (window over/underflow) needs
     * PS.EXCM = 0.  INTLEVEL stays 3, so no other kernel interrupt can nest. */
    movi    a2, PS_C_RUN
    wsr     a2, PS
    rsync

    movi    a2, m5_level3_dispatch
    callx4  a2                      /* C: acks the timer, may context-switch */

    /* Back (possibly much later, after other tasks ran).  Re-enter exception
     * mode before touching EPC3/EPS3 and issuing RFI. */
    movi    a2, PS_C_EXIT
    wsr     a2, PS
    rsync

    l32i    a2, sp, F_PC
    wsr     a2, EPC_3
    l32i    a2, sp, F_PS
    wsr     a2, EPS_3
    l32i    a2, sp, F_SAR
    wsr     a2, SAR
    l32i    a2, sp, F_LBEG
    wsr     a2, LBEG
    l32i    a2, sp, F_LEND
    wsr     a2, LEND
    l32i    a2, sp, F_LCOUNT
    wsr     a2, LCOUNT
    l32i    a2, sp, F_THREADPTR
    wur.threadptr a2
    l32i    a2, sp, F_ACCLO
    wsr     a2, ACCLO
    l32i    a2, sp, F_ACCHI
    wsr     a2, ACCHI

    l32i    a0,  sp, F_A0
    l32i    a3,  sp, F_A0 + 4*3
    l32i    a4,  sp, F_A0 + 4*4
    l32i    a5,  sp, F_A0 + 4*5
    l32i    a6,  sp, F_A0 + 4*6
    l32i    a7,  sp, F_A0 + 4*7
    l32i    a8,  sp, F_A0 + 4*8
    l32i    a9,  sp, F_A0 + 4*9
    l32i    a10, sp, F_A0 + 4*10
    l32i    a11, sp, F_A0 + 4*11
    l32i    a12, sp, F_A0 + 4*12
    l32i    a13, sp, F_A0 + 4*13
    l32i    a14, sp, F_A0 + 4*14
    l32i    a15, sp, F_A0 + 4*15
    l32i    a2,  sp, F_A0 + 4*2
    addi    sp, sp, F_SIZE          /* pop the frame: sp = interrupted sp */
    rsync                           /* EPC3/EPS3 writes must complete before RFI */
    rfi     3                       /* PC <- EPC3, PS <- EPS3: resume the interruptee */
    .size   m5_level3_entry, . - m5_level3_entry

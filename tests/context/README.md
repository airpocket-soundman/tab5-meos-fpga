# ESP32-P4 floating point context validation

Target ABI is ilp32f (single precision hardware FP). Cooperative switches preserve fs0..fs11 and fcsr in a128-byte frame. Asynchronous tick entry preserves all32 FP registers and fcsr in a224-byte frame before calling C. New task frames initialize FP registers and fcsr to zero. MeOS eagerly enables mstatus.FS and does not use IDF lazy FreeRTOS FPU ownership.

Run `python tests/context/audit_fp_frames.py` for frame layout contracts. Compile the two assembly files and context_init.c using the target build flags. Disassembly must contain12 paired fsw/flw in arch_context_switch and32 paired fsw/flw in m5_tick_entry, plus frcsr/fscsr in both.

On hardware call `fp_context_test_run(&result)` from ordinary task context after preemption starts. It creates two tasks with distinct bit patterns in every FP register and fcsr rounding modes. Each task performs64 iterations across cooperative yields (all12 ABI callee FP registers) and call-free integer busy loops (all32 FP registers). The test is bounded to10 seconds; reports per-worker iteration/failure counts, tick count and completion. Passing requires both tasks complete, no errors, and each worker measures at least one timer tick over a live-register loop. Per-worker preempted_loops and loop_ticks record this evidence. This is a practical regression test, not exhaustive instruction coverage; static/disassembly checks independently cover register offsets and save/restore pairs.

IDF-dispatched peripheral interrupt handlers still use IDF's integer-only interrupt frame. They must not execute floating point instructions. Current display/USB ISRs use integer operations. The MeOS tick handler independently saves complete FP state. This change does not provide per-task vector/PIE accelerator registers.

Stack budget: cooperative frame grows64 bytes; tick frame grows144 bytes. Include this extra space when sizing small task stacks.

The32-bit stack initialization host test compiles the actual production C file:

```
zig cc -target x86-windows-gnu -Ikernel/include tests/context/test_stack_init.c -o test-context-init.exe
./test-context-init.exe
```

# Native SL 5.03 port

This runs Toyoda Masashi's original SL animation and ASCII train assets from
[mtoyoda/sl](https://github.com/mtoyoda/sl), pinned to
`923e7d7ebc5c1f009755bdeb789ac25658ccce03`. The exact Git blobs for `sl.c`, `sl.h`
and `LICENSE` are retained in `third_party/sl`; `UPSTREAM.json` records SHA256.
Copyright notices and the upstream redistribution/attribution license remain
in place. This port does not claim authorship of the original program.

`sl_cmd_main(argc, argv)` supports the original `-a`, `-F`, `-l` and `-c`
options, including combinations. Ctrl-C or Escape stops the run (status 130).
A 20-second deadline returns 124; normal completion returns 0. Only one run may
use the adapter at a time. Allocation failure returns without changing terminal
state. No external keyboard input is required for a normal run.

## Adaptation scope

`user/sl/sl_engine.inc` is an explicitly adapted copy of upstream `sl.c`:

- Replace POSIX/curses includes with `sl_compat.h`; use the retained upstream
  `sl.h` assets. Symbol names are prefixed by the wrapper.
- Keep immutable string-pointer tables and movement tables in read-only data.
- Replace the 16 KiB static smoke pool and never-reset count with 128 entries and
  a count in a per-invocation PSRAM context. Capacity is checked before adding
  smoke; every invocation resets smoke and option flags.
- Observe a cancellation flag in the original frame loop. Train drawing,
  animation frames, smoke evolution and option selection retain the original
  algorithms; signal handling and POSIX usleep are replaced by native MeOS
  input/sleep helpers.

This is a minimal curses-shaped adapter for this program, not an ncurses
implementation or Linux executable compatibility. It stores an ASCII canvas
and previous frame in one PSRAM allocation, emits only changed runs with ANSI
cursor positioning, and uses an alternate screen with cursor/autowrap cleanup.
Geometry follows the local LCD console, capped at 160 columns and 80 rows.
Maximum allocation is roughly 28 KiB; no screen/smoke arrays occupy the Shell's
4 KiB stack. Static mutable state is limited to options, geometry, a pointer and
a mutex.

The same console output can be observed over the existing shared SSH console;
there is no independent PTY or SSH-peer-specific geometry. Kernel output can
interleave. Output yields between bursts to let SSH drain its retained log,
but loss-free network animation has not been verified. Forced task termination
is not a supported cleanup method; use Ctrl-C/Escape or allow bounded completion.

## Build and verification

The parent firmware integrates `cmake/sl_meos.cmake`, calls
`m5os_add_sl(target)`, and registers `sl_cmd_main` with the shell. The option is
`M5OS_SL`; sources must be omitted from the generic source glob when disabled.
No libc stdio, POSIX signals, unistd sleep, FreeRTOS or ncurses calls are used by
the production adapter.

`python tests/sl/run_sl_test.py --cc <zig.exe>` verifies upstream hashes and
compiles the real adapted animation with mocked native console/time/allocation.
It runs all engine/option variants repeatedly (more than 90 runs), tiny and
maximum/capped geometries, checks smoke bounds and heap guard bytes, validates
cursor coordinates/alternate-screen restoration, and injects allocation
failure, busy ownership, Ctrl-C, Escape and deadline expiry. These host tests
passed. Target build/flash and serial lifecycle checks now also passed; see below.



## Tab5 serial lifecycle checks, 2026-10-23

The integrated target build and flash passed. Firmware with SL and the public
SSH fingerprint CLI is 1,410,016 bytes. A captured default `sl` run completed in
15.30 seconds and emitted 95329 bytes. Its alternate-screen enter/exit sequences
and return to the shell were present in the serial recording. Ctrl-C during
`sl -aF` and Escape during `sl -lc` each returned in about 0.86 seconds; reruns
then completed successfully.

This is serial-output and lifecycle evidence. No operator assessed visual
quality or LCD appearance, and SSH-delivered animation remains unverified.

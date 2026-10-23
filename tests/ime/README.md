# Shared IME ownership

Run on Windows with `python tests/ime/run_test.py --cc PATH/TO/zig.exe`.
The test compiles the actual IME, converter, romaji parser and dictionary.
It runs the existing Japanese conversion regression and four native threads
performing400 input operations plus initialization, state queries and anchor
updates. Console output deliberately yields while the IME lock is held; chunks
must remain intact. Mutex ownership and pre-scheduler initialization are checked.

The production lock order is IME before display. Current display, console and
OSK drawing paths do not call IME state getters while owning a display batch.
Public IME APIs are task-context operations and must not be called from an ISR
or while holding a display batch. Private initialization avoids recursive locking.
Dictionary background loading remains outside this state mutex; its published
entries are immutable, and dictionary lifetime/loader work is unchanged.

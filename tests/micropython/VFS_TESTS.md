# Streaming VFS adapter fault checks

Run on Windows with `python tests/micropython/run_vfs_test.py --cc PATH/TO/zig.exe`.
The runner compiles the production `user/micropython/meos_vfs.c` into a 32-bit
host executable. `vfs_stub/py` models only the MicroPython C APIs used by the
adapter; these tests do not execute the real Python parser or VM.

The cases cover strict open modes, text/binary protocol selection, relative
paths and embedded NUL rejection, partial reads/writes, closed/stale file
objects, seek bounds, positive errno mapping, eight-slot exhaustion, failed
object allocation before opening, VM teardown including close errors, import
stat and buffered reader EOF/read errors/deferred close errors, stale reader
callbacks, lexer-allocation exception cleanup, PSRAM registry allocation failure,
and balanced registry allocation/free across VM teardown. The fake stream offset is
64-bit to exercise the adapter's 32-bit VFS range checks.

Actual VM execution, byte/text conversion, iterator/context-manager dispatch,
import execution and hardware storage behavior require separate integration
tests against the pinned MicroPython build.

#ifndef FPGA_MEMORY_H
#define FPGA_MEMORY_H
#include <stddef.h>
#include <stdint.h>

/* Host-side synchronous experiment, deliberately not a transport/wire ABI. */
enum fm_status { FM_OK, FM_INVALID, FM_RANGE, FM_BACKEND };
/* Return 0 only after all bytes complete. Nonzero may have partial side effects. */
typedef int (*fm_read_fn)(void *, uint64_t, void *, size_t);
typedef int (*fm_write_fn)(void *, uint64_t, const void *, size_t);
struct fm_region {
    void *backend;
    uint64_t base;
    uint64_t length;
    size_t max_chunk;
    fm_read_fn read;
    fm_write_fn write;
};
struct fm_result {
    enum fm_status status;
    size_t completed; /* Confirmed bytes only; failed chunk may have side effects. */
    size_t calls;
};
/* Rejects address wrap; zero-length is valid. Failure leaves *region unchanged:
 * callers must check the result before using a new configuration. */
enum fm_status fm_init(struct fm_region *, void *, uint64_t, uint64_t,
                       size_t, fm_read_fn, fm_write_fn);
struct fm_result fm_read(const struct fm_region *, uint64_t, void *, size_t);
struct fm_result fm_write(const struct fm_region *, uint64_t, const void *, size_t);
#endif

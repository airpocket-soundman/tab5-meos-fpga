# External-BSS heap boundary

With external BSS enabled, the MeOS heap begins at the next 64-byte boundary
after `_ext_ram_bss_end`. Its end remains unchanged, preserving the IDF reserve
at the top of PSRAM. If BSS consumes the MeOS slice, no heap is returned.
Physical RAM no larger than the reserve is rejected before subtraction.

`sh tools/psram/host_test.sh` compiles the production allocator-boundary code
with external BSS enabled and disabled. It tests BSS overlap/exhaustion,
64-byte alignment, the preserved reserved tail, MMU failures and small RAM.
These host fixtures do not establish physical PSRAM/cache behavior.

## CoreS3 / ESP-IDF5.x

CoreS3 now uses public `esp_psram_is_initialized()` / `esp_psram_get_size()`
and the DATA MMU mapping of physical address0. It validates every mapped page
and the final byte, and never derives the base from `SOC_EXTRAM_DATA_HIGH`.
Only a contiguous, in-window, MEMMAP-only region is handed to MeOS. Failed
initialization, partial/discontinuous mapping, IDF heap sharing, PSRAM XIP or
an exhausted external-BSS/noinit boundary returns no heap. External BSS/noinit
is excluded with64-byte alignment. The version guard preserves the pinned
IDF4.4.3 startup path and its documented top-of-window mapping. IDF4 private
symbols are compiled only for SDK4, never SDK5.

`python tests/test_cores3_psram.py --cc PATH_TO_ZIG` exercises the production
CoreS3 code with the existing MMU/PSRAM host stubs; it does not prove physical
CoreS3 PSRAM behavior. SDK5 additionally requires `esp_psram` and `esp_mm` on CoreS3; SDK4 keeps
its original Xtensa component dependency.

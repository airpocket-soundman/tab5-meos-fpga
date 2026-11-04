# Trusted native M5EX execution on ESP32-P4

ESP-IDF's default `CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y` locks internal firmware
code as RX and the remaining internal heap as RW, without execute permission.
Copying a native image into that heap and using `fence.i` does not grant execute
permission. `MALLOC_CAP_EXEC` is also unavailable for these heaps under this
configuration. The MeOS P4 loader now refuses such a load with `ERR_NOSYS`
instead of jumping into protected memory.

For a dedicated build that runs trusted native applications, select the
SDK-supported RWX internal-RAM policy explicitly:

```sh
export M5OS_BOARD=tab5
idf.py -B build-tab5-native -DSDKCONFIG=sdkconfig.tab5.native \
  -DM5OS_NATIVE_M5EX=ON -DM5OS_MODULES=fpga_parlio build
```

Use a **fresh sdkconfig path**: the option appends
`sdkconfig.defaults.native_m5ex`, whose defaults do not override values already
saved in an existing sdkconfig. For an existing dedicated configuration, change
`CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT` explicitly and verify it is disabled in the
resulting generated configuration before building. The normal firmware defaults
and the three published size profiles are unchanged. `large_fpga` can replace
the minimal module selection when building against its supported full-feature
SDK; that is a separate integration/size comparison.

This changes only the SDK's IRAM/DRAM split policy. Other PMP/PMA protection
regions remain configured; no runtime unlock or replacement protection table
is installed. M5EX is already a trusted-machine-code format: native programs
execute with kernel hardware access. Its manifest permission bits govern OS
APIs and do not create an isolation boundary.

The image remains an application-owned internal allocation, reclaimed by its
original allocator/app ID. P4 reserves padding and aligns the image to128 bytes
so cache publication cannot invalidate an unrelated allocation. After copying,
BSS initialization and relocation, the loader performs data-cache writeback,
instruction-cache invalidation and `fence.i` in that order. Cache errors abort
and reclaim the application before any entry call. PSRAM remains refused as a
native code allocation by this HAL. ESP32-S3 retains its existing instruction
alias; the shared publication API now returns success/failure.

`tests/test_exec_p4.py` compiles the actual P4 HAL against a cache model with
split enabled/disabled, and the actual loader against lifecycle mocks. It
checks permission refusal, cache order/error propagation, aligned image bounds
and allocation cleanup without launch. These tests do not substitute for a
target execution test.

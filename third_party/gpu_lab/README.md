# GPU LAB renderer adapted for native MeOS

Source: airpocket-soundman/tab5-fpga-gpu, commit
`d4d89e308b4a8aa799199b20e7d47c96b536e3fd`.
`provenance.json` identifies the exact original and adapted source hashes.
Original paths are retained under this directory. This is a local adaptation,
not a shared wire protocol, FPGA renderer, LVGL/BSP port or copied ESP-IDF app.

Changes relative to the original:

- `main/lab.hpp`: remove the unused virtual backend base and dynamic ownership;
  the CPU owns one explicitly allocated FlowState, with copying disabled.
- `main/lab.cpp`: flow particles/trails use bounded `NativeBuffer` storage, and
  FlowState uses placement construction with checked `gfxbench_alloc/free` hooks.
  Partial allocation failure unwinds all buffers and returns false. MeOS hooks
  use PSRAM directly. No newlib malloc/free or global C++ new/delete is used.
- `reference/cpp/integer_raster.hpp`: retain FrameView rasterization; remove the
  owning Frame/vector and coverage collection used by upstream host tests.
  Firmware calls already pass `collect_coverage=false`. Numeric rules are unchanged.
- Eight scene algorithms, load counts, frame-driven animation, 640x360 RGB565,
  integer G0 rules, workload counters, sampling and checksum remain unchanged.

The native application is `user/gfxbench/gfxbench_app.cpp`. The renderer's
allocation hooks are supplied by that adapter, or by the host test fixtures.
The ESP-IDF UI, tasks, USB API and system allocator are not imported.

`tests/test_gfxbench.py --reference PATH --cc PATH_TO_ZIG` verifies the pinned
upstream files, then compares color/depth/work output for all 60 combinations
(legacy 8 scenes + integer 2 scenes, 3 loads, frames 0 and 2). Both are compiled
with the same compiler and `-ffp-contract=off` so host math-library differences
do not masquerade as port differences. This does not promise host/Tab5 float
bit identity. Firmware object undefined-symbol inspection also confirms no
STL heap, global new/delete, malloc/free or FreeRTOS entry points in the renderer.

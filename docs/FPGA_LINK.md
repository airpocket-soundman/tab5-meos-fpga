# FPGA bringup-v0 protocol groundwork

`fpga status` reports that no physical backend exists. `fpga --mock id`, `fpga --mock echo 0x55 0xaa`, `fpga --mock invert 0x55 0xaa` and `fpga --mock selftest` explicitly select a software endpoint. No GPIO, PARLIO, DMA, DDR3 or electrical timing is exercised.

The independent codec uses 8-byte frames and CRC16/CCITT-FALSE. ID, echo and inversion responses validate framing, sequence, status and data. Transport callbacks support fragmented progress, deadlines, cancellation and cleanup. A bounded poll count also terminates a stalled clock. Clients require owner serialization; the reentry guard is not a cross-task lock. Backends must honor the deadline and buffer contract, including bounded finish/cleanup.

583 golden vectors from GPU revision 3b1ace3d91c6305ec0e32378f8819fce208c2aa7 matched response bytes and acceptance. Fixture provenance/hashes are in tests/fixtures/fpga_bringup_v0_source.json. Eight fragment sizes passed 4104 pattern exchanges and 96 fault checks; additional tests cover corruption, late completion, cancellation, sequence wrap, recursive entry and CLI rejection. Native Tab5 mock selftest passed 513 pattern exchanges and 12 fault checks, including ten additional successful recovery exchanges. Heap usage was unchanged. This validates software execution on Tab5, not FPGA connectivity.

Physical implementation requires the latest carrier pin map, IO ownership, direction/turnaround timing, safe power sequencing and timeout recovery to be verified first. Evidence: [host](validation/fpga-link-host-result.json), [Tab5 mock](validation/fpga-link-hardware-result.json). Run `python tests/test_fpga_link.py --cc PATH_TO_COMPILER`.

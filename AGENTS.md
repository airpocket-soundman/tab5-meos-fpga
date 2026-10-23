# Project working instructions

- FPGA target: Tang Primer 20K Core and its128 MiB DDR3, using the shared `tab5-tang-primer20k-carrier` board.
- Before FPGA, GUI, GPIO allocation, transport or display-path changes, fetch/check the carrier repository's current remote revision and inspect its actual specification, final pin assignment, generated hardware and audit records. Preserve unrelated dirty checkouts. Record the reviewed SHA and relevant changes in `docs/FPGA_CARRIER_REQUIREMENTS.md`.
- Treat the carrier as the source of shared electrical/pin/power specifications. Compare against MeOS IO ownership, display and active peripherals; do not assume Linux/ESP-IDF drivers run unchanged under MeOS.
- Review `docs/FPGA_CARRIER_REQUIREMENTS.md` before implementation. New physical requirements must be recorded before fabrication, with unresolved decisions distinguished from verified compatibility. Do not infer transfer speed, safe power sequencing or internal LCD connectivity from DDR capacity alone.
- GitHub repositories owned by airpocket-soundman belong under `D:\github\airpocket-soundman`; Factory-IoT repositories under `D:\github\Factory-IoT`.

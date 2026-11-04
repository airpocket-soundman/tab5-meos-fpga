#pragma once
#include "fpga_wire.h"
#define FPGA_TILE_TEST_PIXELS 1024u
#define FPGA_TILE_TEST_SCENES 6u
#define FPGA_TILE_TEST_TIMEOUT_MAX_US 30000000u
typedef struct {
    uint8_t body[FPGA_WIRE_TILE_BYTES];
    /* Actual received color words only. Never filled from the CPU reference.
     * Use only when run returns0 AND report.image_valid is true. */
    uint16_t received_color[FPGA_TILE_TEST_PIXELS];
    uint16_t records[64u*10u]; /*Bounded extended-request staging, never stack.*/
} fpga_tile_test_workspace_t;
typedef struct {
    uint32_t scenes,pixels,transactions,scene,plane,pixel;
    uint16_t expected,received,fractal_info;
    uint32_t cycles[2]; /*Mandelbrot, Julia, only when cycles_valid[i]*/
    uint64_t elapsed_us;
    int error;
    bool cycles_valid[2],image_valid;
} fpga_tile_test_report_t;
/* Caller owns session and both distinct heap workspaces. Fixed four triangle
 * scenes, optionally Mandelbrot/Julia. Checks1024color+1024depth words per scene,
 * never displays, retries or substitutes CPU output. First error stops the run.
 * tagged requires prior LINK_CAPS negotiation. TILE_CAPS is queried through the
 * current transport/tag mode only when not already granted. total timeout is
 * independent of per-command engine timeout (each command is <=1second).
 * Reports progress/mismatch even on failure. Failure always clears image_valid.
 * Stack use stays small; neither expected nor received full-frame arrays live
 * on stack. Final successful color image is the verified Julia tile when used. */
int fpga_tile_test_run(fpga_wire_t *wire,fpga_tile_test_workspace_t *workspace,
                       fpga_tile_test_report_t *report,bool tagged,
                       bool include_fractals,uint32_t total_timeout_us);
/* Three exact full-plane comparisons: count grid triangles, equal-depth
 * recoloring (must retain first image), mixed nearer/farther recoloring.
 * count1..64; count64 emits three maximum1290-byte requests. No fallback when
 * BATCH/TILE capabilities are absent. Same report/image-validity rules as above. */
int fpga_tile_batch_test_run(fpga_wire_t *wire,fpga_tile_test_workspace_t *workspace,
                             fpga_tile_test_report_t *report,bool tagged,
                             unsigned count,uint32_t total_timeout_us);

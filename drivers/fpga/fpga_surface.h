#pragma once
#include "fpga_wire.h"

#define FPGA_SURFACE_WIDTH 32u
#define FPGA_SURFACE_HEIGHT 32u
#define FPGA_SURFACE_PIXELS 1024u
#define FPGA_SURFACE_TRIANGLES_MAX 64u
#define FPGA_SURFACE_TIMEOUT_MAX_US 5000000u

typedef struct {
    int16_t ax,ay,bx,by,cx,cy; /*Signed Q12.4 raw coordinates.*/
    uint16_t za,zb,zc,color;   /*Unsigned depth, RGB565.*/
} fpga_surface_triangle_t;

typedef struct {
    uint16_t records[FPGA_SURFACE_TRIANGLES_MAX*10u];
    uint8_t body[FPGA_WIRE_TILE_BYTES];
    uint16_t color[FPGA_SURFACE_PIXELS]; /*Actual validated wire data only.*/
} fpga_surface_workspace_t;

typedef struct {
    fpga_wire_t *wire;
    fpga_surface_workspace_t *workspace;
    uint64_t generation,image_generation;
    uint32_t batches,triangles,readbacks,presentations;
    unsigned queued;
    int last_error;
    bool tagged,busy,active,poisoned,image_valid,presented;
} fpga_surface_t;

/* One reusable32x32 GPU tile, not a window system, framebuffer or DMA owner.
 * Caller allocates both distinct workspaces, owns/serializes the ENTIRE physical
 * wire session and keeps this object/workspace alive throughout each operation.
 * Surface busy rejects same-object reentry; it is NOT a cross-task/device lock.
 * No automatic batching, replay, fallback or transport/IO reconfiguration.
 * Init is for a fresh session, not recovery after an unknown device outcome. */
int fpga_surface_init(fpga_surface_t *,fpga_wire_t *,fpga_surface_workspace_t *,bool tagged);
/* Explicit CLEAR begins one update; TILE/BATCH/triangle capabilities are
 * checked on this current transport. Invalid arguments do not change state.
 * Any command/cancellation/deadline failure poisons the surface: caller must
 * close/reset the physical session before initializing a fresh instance. */
int fpga_surface_begin(fpga_surface_t *,uint16_t background,uint32_t timeout_us);
int fpga_surface_queue(fpga_surface_t *,const fpga_surface_triangle_t *);
int fpga_surface_flush(fpga_surface_t *,uint32_t timeout_us);
/* Flush queued geometry, then receive+CRC-validate the complete color tile.
 * Only full success publishes a new image generation; no CPU pixels are used. */
int fpga_surface_finish(fpga_surface_t *,uint32_t timeout_us);
const uint16_t *fpga_surface_image(const fpga_surface_t *,uint64_t *generation);
/* Draw only the committed cached image through one display begin/end batch.
 * No FPGA/wire access: physical lease may already have been closed. Caller
 * controls console/compositor ownership. Entire scaled tile must fit the LCD;
 * no silent clipping or direct-draw fallback if a display batch is unavailable.
 * Display failure does not erase the valid GPU cache, but presented is false.
 * Completion/failure is returned; callers decide any explicit presentation retry. */
int fpga_surface_present(fpga_surface_t *,int x,int y,unsigned scale);

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Dock native finite DMA only. Caller owns all pins and serializes the handle.
 * No boot hook. Open allocates/claims peripheral resources but never maps pads.
 * tx restores CLK GPIO LOW and all DATA inputs on every attempted transfer.
 * rx requires CLK HIGH, DIR HIGH, READY HIGH and all DATA already inputs.
 * Adapter-owned peripheral polls use an absolute deadline plus poll cap.
 * Cache/allocator operations retain their platform implementation contracts.
 * No retry, GPIO fallback, FreeRTOS queue or ISR allocation. */
typedef struct hal_parlio hal_parlio_t;
typedef bool (*hal_parlio_cancel_t)(void *);
enum { HAL_PARLIO_CANCELLED = -20, HAL_PARLIO_MAX_TX = 1290,
       HAL_PARLIO_MAX_RX = 2176 };
typedef struct {
    uint32_t frequency_hz, rx_frequency_hz, tx_attempts, tx_completed, rx_attempts, rx_completed;
    uint32_t tx_bytes, rx_bytes, elapsed_us, parlio_raw, dma_raw;
    int tx_channel, rx_channel, last_error;
    bool tx_phase_verified;
    uint32_t last_rx_bytes;
    bool rx_phase_verified, dma_quarantined;
    int gate_gpio, etm_channel, gpio_task_channel;
    bool rx_gate_verified, rx_gate_closed, gate_event_seen, gate_task_seen;
    /* Gate readiness bits: armed, IDs, ETM enabled/event/task, GPIO binding,
     * GPIO channel, output enable, CPU OE, CPU route, LOW (bits 0..10). */
    uint32_t gate_ready_mask, gate_gpio_enable, gate_gpio_mux, gate_gpio_task;
    uint32_t gate_etm_event, gate_etm_task;
    const char *stage;
} hal_parlio_stats_t;
int hal_parlio_open(uint32_t frequency_hz, hal_parlio_t **out);
/* Caller serializes this with transfer/close. Idle software-only selection;
 * no clock/pad changes until the next RX. Default is the open/TX frequency.
 * Supported RX rates: 500k, 1M, 2M, 5M, 8M, 10M, 16M and 20M Hz.
 * Accepted rates describe exact divider support, not hardware validation. */
int hal_parlio_set_rx_frequency(hal_parlio_t *, uint32_t frequency_hz);
int hal_parlio_tx(hal_parlio_t *, const uint8_t *, size_t, uint64_t deadline_us,
                  hal_parlio_cancel_t, void *);
int hal_parlio_rx(hal_parlio_t *, uint8_t *, size_t capacity, size_t *received,
                  uint64_t deadline_us, hal_parlio_cancel_t, void *);
void hal_parlio_get_stats(const hal_parlio_t *, hal_parlio_stats_t *);
/* True only after DMA is quiescent and all resources actually released.
 * False retains ownership/storage; caller must retain the IO pin lease. */
bool hal_parlio_close(hal_parlio_t *);

#pragma once
/* Original MeOS OS definitions for Apache NimBLE's NPL interface. */
#include <stdbool.h>
#include <stdint.h>
#define BLE_NPL_OS_ALIGNMENT 4
#define BLE_NPL_TIME_FOREVER UINT32_MAX
typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;
struct ble_npl_eventq;
struct ble_npl_event {
    struct ble_npl_event *next;
    struct ble_npl_eventq *queue;
    void (*fn)(struct ble_npl_event *);
    void *arg;
};
struct ble_npl_eventq {struct ble_npl_event *head,*tail;bool initialized;};
struct ble_npl_callout {
    struct ble_npl_event event;
    struct ble_npl_eventq *queue;
    uint32_t deadline;
    bool active,registered;
};
struct ble_npl_mutex {void *owner;uint16_t depth;bool initialized;};
struct ble_npl_sem {uint16_t tokens;bool initialized;};

#pragma once
#include <m5os/types.h>
#include <m5os/app.h>
#include <m5os/errno.h>

/* Statically linked applications.  The table (with the permissions each app is
 * granted) is in apps/apps.c; app_start("name") in kernel/app/app.c runs them. */
const app_def_t *apps_def_table(int *count);

/* Observable results, used by the demo apps and the regression tests. */
extern volatile uint32_t app_hog_count;
extern volatile uint32_t app_rogue_mask;      /* bit0 display denied, bit1 file write denied, bit2 spawn denied, bit3 console allowed, bit4 ipc denied, bit5 window denied */
extern volatile uint32_t app_leaker_ready;
extern volatile uint32_t app_family_child_pid;
extern char app_calc_display[16];
extern volatile uint32_t app_intruder_target;
extern volatile uint32_t app_intruder_result;     /* bit0 draw denied, bit1 destroy denied, bit2 focus denied */
extern volatile uint32_t app_calc_painted;

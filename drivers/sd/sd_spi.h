#pragma once
#include <m5os/blockdev.h>

/* Probe and initialise the card.  Returns the block device, or NULL if there is
 * no (working) card.  Safe to call once, from task context. */
blockdev_t *sd_init(void);
void        sd_print_info(void);

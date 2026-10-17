#include "../apps.h"

volatile uint32_t app_hog_count;

/* Deliberately never yields or sleeps: only preemption (and stop/kill) can end it. */
void hog_main(void *arg)
{
    (void)arg;
    for (;;) app_hog_count++;
}

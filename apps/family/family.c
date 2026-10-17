/* A parent app with a child task: stopping the app must end both. */
#include "../apps.h"
#include <m5os/task.h>

volatile uint32_t app_family_child_pid;
#define CHILD_PERIOD_MS   100u

static void child(void *arg)
{
    (void)arg;
    for (;;) task_sleep(CHILD_PERIOD_MS);
}

void family_main(void *arg)
{
    (void)arg;
    int pid = task_create("family-child", child, NULL, TASK_PRIO_DEFAULT);   /* inherits this app */
    app_family_child_pid = pid > 0 ? (uint32_t)pid : 0;
    for (;;) task_sleep(1000);
}

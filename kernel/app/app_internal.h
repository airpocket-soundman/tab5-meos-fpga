#pragma once
#include <m5os/app.h>

/* Used by the loader (m5ex_loader.c) to start apps whose code it prepares itself. */
app_t *app_reserve(const char *name, uint32_t permissions);
int    app_launch(app_t *a, void (*task_entry)(void *), void *arg, uint8_t priority, size_t stack_size);
void   app_abort(app_t *a);

#pragma once
/* Serial shell.  Runs as a task; talks to the OS only through the VFS. */
void shell_task(void *arg);

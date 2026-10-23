#pragma once
#include <m5os/types.h>
/* Pass the complete argv, including "bt". Output uses the caller's shell/SSH
 * stream; status and pairing prompts are never printed asynchronously. */
int bt_app_command(int argc,char **argv,void (*output)(const char *text));

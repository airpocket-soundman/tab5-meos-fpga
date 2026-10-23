#pragma once
#include <m5os/types.h>
/* Explicit local destructive-scope test: creates only a fresh /sd file via EXCL.
 * No existing overwrite, delete or formatting. Leaves its test file in place. */
bool sftp_storage_selftest(const char *root);

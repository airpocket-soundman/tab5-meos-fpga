#pragma once
/* Full-screen file manager in the style of the VZ Editor's filer: a multi-column directory list,
 * marks, one-key copy / move / delete / make-directory / rename, a viewer, a key guide on the bottom
 * line.  Runs inside the calling task (the shell) until the user quits. */
void filer_run(const char *start_dir);

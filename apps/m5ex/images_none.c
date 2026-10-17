/* Boards with no M5EX images built for them (the sample apps are Xtensa machine code). */
#include <m5os/m5ex.h>
const m5ex_image_t *m5ex_embedded(int *count) { *count = 0; return 0; }

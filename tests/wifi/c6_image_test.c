#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../drivers/wifi/c6_mcu_image.h"
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t *b=malloc((size_t)n);assert(b);assert(fread(b,1,n,f)==(size_t)n);fclose(f);
    assert(c6_mcu_image_valid(b,n));
    assert(!c6_mcu_image_valid(NULL,n));assert(!c6_mcu_image_valid(b,n-1));
    b[12]^=1;assert(!c6_mcu_image_valid(b,n));b[12]^=1;
    b[1000]^=1;assert(!c6_mcu_image_valid(b,n));b[1000]^=1;
    b[n-1]^=1;assert(!c6_mcu_image_valid(b,n));
    free(b);puts("PASS: pinned C6 application, chip, truncation, segment checksum, embedded SHA");return 0;
}

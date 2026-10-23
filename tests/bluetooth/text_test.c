#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../drivers/bluetooth/npl/meos_text.inc"
int main(void) {
 char text[40];memset(text,'!',sizeof text);
 assert(meos_bt_sprintf(text,"0x%04x",0x1812u)==6&&!strcmp(text,"0x1812"));
 assert(meos_bt_sprintf(text,"0x%08lx",0xfffffffful)==10&&!strcmp(text,"0xffffffff"));
 assert(meos_bt_sprintf(text,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
  0x00u,0x01u,0x02u,0x03u,0x04u,0x05u,0x06u,0x07u,0x08u,0x09u,0x0au,0x0bu,0x0cu,0x0du,0x0eu,0x0fu)==36);
 assert(!strcmp(text,"00010203-0405-0607-0809-0a0b0c0d0e0f")&&text[37]=='!');
 assert(meos_bt_sprintf(text,"%09x",0u)==-1&&!text[0]);
 assert(meos_bt_sprintf(text,"%s","unsupported")==-1&&!text[0]);
 puts("NimBLE bounded UUID formatter tests PASS");
}

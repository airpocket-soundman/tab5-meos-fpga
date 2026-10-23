/* UUID-only sprintf replacement. The upstream UUID formatter uses unsigned
 * hex fields and literals only; avoid newlib's FreeRTOS-backed stdio locks.
 * Its destination contract is BLE_UUID_STR_LEN (37 including terminator). */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
int meos_bt_sprintf(char *destination,const char *format,...) {
    if(!destination||!format)return -1;
    char out[37];size_t used=0;va_list arguments;va_start(arguments,format);
    for(const char *p=format;*p;p++) {
        if(*p!='%') {if(used==sizeof out-1)goto invalid;out[used++]=*p;continue;}
        p++;unsigned width=0;
        while(*p>='0'&&*p<='9') {width=width*10u+(unsigned)(*p-'0');if(width>8)goto invalid;p++;}
        bool wide=false;if(*p=='l') {wide=true;p++;}
        if(*p!='x')goto invalid;
        uint32_t value=wide?(uint32_t)va_arg(arguments,unsigned long):va_arg(arguments,unsigned int);
        char digits[8];unsigned length=0;
        do {digits[length++]="0123456789abcdef"[value&15u];value>>=4;} while(value);
        if(width<length)width=length;
        if(width>sizeof out-1-used)goto invalid;
        for(unsigned i=length;i<width;i++)out[used++]='0';
        while(length)out[used++]=digits[--length];
    }
    out[used]=0;for(size_t i=0;i<=used;i++)destination[i]=out[i];
    va_end(arguments);return (int)used;
invalid:
    va_end(arguments);destination[0]=0;return -1;
}

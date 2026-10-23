#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../drivers/wifi/hosted_wire.h"

int main(void)
{
    /* Observed 40-byte RX header, followed by a non-secret startup event built
     * from official NG event/TLV definitions (not a captured live payload). */
    uint8_t ng[40] = {
        3,0,0,0,28,0,12,0,0,0,0,0,
        1,0,21,0,20,0,0,0,
        3,1,0x0d, 0,1,1,
        1,12,'E','S','P',1,0,0,0,0,0,0,0,0
    };
    assert(hosted_wire_is_ng_boot(ng, sizeof ng));
    /* Ordinary MCU PRIV startup uses plen@2, offset@4 and IF=5. */
    uint8_t mcu[40] = {5,0,28,0,12,0,0,0,0,0,0,0x33,0x22,26};
    assert(!hosted_wire_is_ng_boot(mcu, sizeof mcu));
    mcu[0] = 3; /* MCU serial responses must never be mistaken for NG boot. */
    assert(!hosted_wire_is_ng_boot(mcu, sizeof mcu));
    for (size_t n = 0; n < sizeof ng; n++) assert(!hosted_wire_is_ng_boot(ng,n));
    uint8_t bad[40];
    memcpy(bad,ng,sizeof bad); bad[12]=2; assert(!hosted_wire_is_ng_boot(bad,sizeof bad));
    memcpy(bad,ng,sizeof bad); bad[14]=20; assert(!hosted_wire_is_ng_boot(bad,sizeof bad));
    memcpy(bad,ng,sizeof bad); bad[21]=25; assert(!hosted_wire_is_ng_boot(bad,sizeof bad));
    memcpy(bad,ng,sizeof bad); bad[22]=0x0c; assert(!hosted_wire_is_ng_boot(bad,sizeof bad));
    memcpy(bad,ng,sizeof bad); bad[25]=0; assert(!hosted_wire_is_ng_boot(bad,sizeof bad));
    assert(!hosted_wire_is_ng_boot(NULL,40));
    puts("PASS: NG startup recognition, MCU isolation, truncation and malformed TLVs");
    return 0;
}

#include "i2c_spi_mock.h"
enum { SCK=1,MOSI=2,MISO=3,CS=4 };
static unsigned edges,samples,selects,deselects;
static uint8_t output,input=0xa6;
static unsigned mode;
static bool tracing;
static void changed(int pin,int value) {
    assert(pins_owned[pin]);
    int previous=levels[pin];levels[pin]=value;
    if(!tracing)return;
    if(pin==CS && previous!=value) { if(value)deselects++;else selects++; }
    if(pin==SCK && previous!=value && !levels[CS]) {
        unsigned edge=edges++;
        bool sample=((mode&1)?(edge&1):!(edge&1));
        if(sample)output=(uint8_t)((output<<1)|levels[MOSI]);
    }
}
int hal_gpio_read(int pin) {
    assert(pin==MISO&&!levels[CS]);
    int idle=(mode>>1)&1;
    assert(levels[SCK]==((mode&1)?idle:!idle));
    return(input>>(7-(samples++%8)))&1;
}
#include "../../drivers/io/spi.c"
static uint32_t h;
static void close_owner_at_start(void) {
    if (buses[0].running) {
        owner_alive=false;
        buses[0].cancelled=true;
        on_owner_check=NULL;
    }
}

static unsigned cancel_after;
static void cancel_transfer(void){if(now>=cancel_after){on_delay=NULL;assert(io_spi_cancel(1,h)==0);}}
static void reset_trace(void){edges=samples=selects=deselects=output=0;tracing=true;}
int main(void) {
    io_spi_config_t c={.sck=SCK,.mosi=MOSI,.miso=MISO,.cs=CS,.frequency_hz=100000,.transfer_timeout_us=10000};
    uint32_t stale=0;
    for(mode=0;mode<4;mode++) {
        c.mode=(uint8_t)mode;tracing=false;
        assert(io_spi_open(1,&c,&h)==0&&levels[CS]==1&&levels[SCK]==(int)(mode>>1));
        if(stale)assert(io_spi_transfer(1,stale,NULL,NULL,0)==ERR_BADF);
        uint32_t other;assert(io_spi_open(1,&c,&other)==ERR_BUSY);
        reset_trace();uint8_t tx=0x95,rx=0;
        assert(io_spi_transfer(1,h,&tx,&rx,1)==0);
        assert(rx==input&&output==tx&&edges==16&&samples==8&&selects==1&&deselects==1);
        reset_trace();assert(io_spi_transfer(1,h,NULL,NULL,1)==0&&output==0xff);
        reset_trace();cancel_after=(unsigned)now+15;on_delay=cancel_transfer;
        assert(io_spi_transfer(1,h,&tx,&rx,1)==IO_ERR_CANCELLED&&levels[CS]&&levels[SCK]==(int)(mode>>1));
        tracing=false;
        assert(io_spi_transfer(2,h,&tx,&rx,1)==ERR_BADF);
        assert(io_spi_transfer(1,h,&tx,&rx,4097)==ERR_INVAL);
        uint32_t old=h;stale=h;assert(io_spi_close(1,h)==0);
        assert(io_spi_transfer(1,old,&tx,&rx,1)==ERR_BADF);
    }
    c.mode=0;c.transfer_timeout_us=5;
    assert(io_spi_open(1,&c,&h)==0);
    uint8_t tx=0;assert(io_spi_transfer(1,h,&tx,NULL,1)==ERR_TIMEOUT&&levels[CS]);
    owner_alive=false;io_spi_release_owner(1);owner_alive=true;assert(!pins_owned[SCK]&&!pins_owned[CS]);
    c.cs=31;assert(io_spi_open(1,&c,&h)==ERR_PERM&&!pins_owned[SCK]);
    c.cs=CS;c.miso=MOSI;assert(io_spi_open(1,&c,&h)==ERR_INVAL&&!pins_owned[SCK]);
    c.miso=MISO;c.mode=4;assert(io_spi_open(1,&c,&h)==ERR_INVAL);
    c.mode=0;
    for(unsigned i=0;i<4;i++) {
        c.sck=1+4*i;c.mosi=2+4*i;c.miso=3+4*i;c.cs=4+4*i;
        assert(io_spi_open(1,&c,&h)==0);
    }
    c.sck=17;c.mosi=18;c.miso=19;c.cs=20;
    assert(io_spi_open(1,&c,&h)==ERR_BUSY&&!pins_owned[17]);
    io_spi_release_owner(1);
    for(unsigned i=1;i<=16;i++)assert(!pins_owned[i]);
    c.sck=SCK;c.mosi=MOSI;c.miso=MISO;c.cs=CS;
    assert(io_spi_open(1,&c,&h)==0);
    on_release=die_after_pin_release;
    if(setjmp(killed)==0){io_spi_close(1,h);assert(!"fault not reached");}
    lock.owner=NULL;
    assert(lookup(1,h)==NULL);
    uint32_t replacement;
    assert(io_spi_open(2,&c,&replacement)==0);
    unsigned touches_before=pin_touches,releases_before=releases;
    io_spi_release_owner(1);
    assert(pin_touches==touches_before&&releases==releases_before);
    assert(pins_owned[CS]==2&&pins_owned[SCK]==2);
    assert(io_spi_close(2,replacement)==0);
    c.sck=SCK;c.mosi=MOSI;c.miso=MISO;c.cs=CS;
    assert(io_spi_open(1,&c,&h)==0);
    touches_before=pin_touches;
    on_owner_check=close_owner_at_start;
    assert(io_spi_transfer(1,h,NULL,NULL,1)==ERR_PERM);
    assert(pin_touches==touches_before&&buses[0].cancelled);
    io_spi_release_owner(1);owner_alive=true;
    c.frequency_hz=0;assert(io_spi_open(1,&c,&h)==ERR_INVAL);
    puts("PASS SoftSPI modes0-3/MSB/full duplex/CS/cancel/timeout/owner/closed handle/reserved pins");
}

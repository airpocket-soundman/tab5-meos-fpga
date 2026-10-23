#include "i2c_spi_mock.h"
enum { SDA=1,SCL=2 };
static uint8_t input[256];
static unsigned input_count,input_at, starts,stops;
static bool trace, hold_clock;
static unsigned stretch_reads;
static struct { unsigned bits,count;uint8_t value,bytes[32];int wire[128];unsigned clocks; } segments[8];
static void changed(int pin,int value) {
    assert(pins_owned[pin]);
    int previous=levels[pin];levels[pin]=value;
    if(!trace || previous==value)return;
    if(pin==SDA && levels[SCL]) {
        if(value)stops++;
        else { assert(starts<8);starts++; }
    }
    if(pin==SCL && value && starts) {
        unsigned s=starts-1;
        assert(segments[s].clocks<128);
        segments[s].wire[segments[s].clocks++]=levels[SDA];
        if(segments[s].bits<8)segments[s].value=(uint8_t)((segments[s].value<<1)|levels[SDA]);
        if(++segments[s].bits==9) {
            assert(segments[s].count<32);
            segments[s].bytes[segments[s].count++]=segments[s].value;
            segments[s].bits=segments[s].value=0;
        }
    }
}
int hal_gpio_read(int pin) {
    if(pin==SCL) { if(hold_clock)return 0;if(stretch_reads){stretch_reads--;return 0;}return levels[pin]; }
    assert(pin==SDA);
    assert(input_at<input_count);
    return input[input_at++];
}
#include "../../drivers/io/i2c.c"
static uint32_t h;
static void close_owner_at_start(void) {
    if (buses[0].running) {
        owner_alive=false;
        buses[0].cancelled=true;
        on_owner_check=NULL;
    }
}

static unsigned cancel_after;
static void cancel_during_transfer(void) {
    if(now>=cancel_after){on_delay=NULL;assert(io_i2c_cancel(1,h)==0);}
}
static void reset_trace(void) {
    input_count=input_at=starts=stops=0;
    memset(segments,0,sizeof segments);
    trace=true;
}
static void sample(unsigned n){assert(input_count<sizeof input);input[input_count++]=(uint8_t)n;}
static void byte(uint8_t n){for(int bit=7;bit>=0;bit--)sample((n>>bit)&1);}
int main(void) {
    io_i2c_config_t c={.sda=SDA,.scl=SCL,.frequency_hz=100000,.stretch_timeout_us=20,.transfer_timeout_us=10000};
    assert(io_i2c_open(1,&c,&h)==0 && claims==1);
    uint32_t other;
    assert(io_i2c_open(1,&c,&other)==ERR_BUSY&&claims==1);
    uint8_t tx=0x23,rx[2]={0};
    reset_trace();sample(1);sample(0);sample(0);sample(1);sample(0);byte(0xa5);byte(0x5a);
    stretch_reads=3;
    assert(io_i2c_transfer(1,h,0x50,&tx,1,rx,2)==0);
    assert(rx[0]==0xa5&&rx[1]==0x5a&&starts==2&&stops==1&&input_at==input_count);
    assert(segments[0].bytes[0]==0xa0&&segments[0].bytes[1]==0x23);
    assert(segments[1].bytes[0]==0xa1);
    assert(segments[1].wire[17]==0&&segments[1].wire[26]==1); /* ACK then final NACK */
    reset_trace();sample(1);sample(0);byte(0x42);
    assert(io_i2c_transfer(1,h,0x50,NULL,0,rx,1)==0&&rx[0]==0x42&&starts==1&&stops==1);
    assert(segments[0].bytes[0]==0xa1);
    reset_trace();sample(1);sample(0);sample(0);
    assert(io_i2c_transfer(1,h,0x50,&tx,1,NULL,0)==0&&starts==1&&stops==1);
    reset_trace();sample(1);sample(1);
    assert(io_i2c_transfer(1,h,0x50,NULL,0,NULL,0)==IO_ERR_NACK&&stops==1);
    reset_trace();hold_clock=true;uint64_t before=now;
    assert(io_i2c_transfer(1,h,0x50,NULL,0,NULL,0)==ERR_TIMEOUT);
    assert(now-before<100&&levels[SDA]&&levels[SCL]);hold_clock=false;
    reset_trace();sample(1);cancel_after=(unsigned)now+15;on_delay=cancel_during_transfer;
    assert(io_i2c_transfer(1,h,0x50,&tx,1,NULL,0)==IO_ERR_CANCELLED&&stops==1);
    trace=false;
    assert(io_i2c_transfer(2,h,0x50,NULL,0,NULL,0)==ERR_BADF);
    assert(io_i2c_transfer(1,h,0x80,NULL,0,NULL,0)==ERR_INVAL);
    assert(io_i2c_transfer(1,h,0x50,NULL,1,NULL,0)==ERR_INVAL);
    assert(io_i2c_transfer(1,h,0x50,&tx,4097,NULL,0)==ERR_INVAL);
    uint32_t stale=h;
    assert(io_i2c_close(1,h)==0&&releases==1);
    assert(io_i2c_open(1,&c,&h)==0&&h!=stale);
    assert(io_i2c_transfer(1,stale,0x50,NULL,0,NULL,0)==ERR_BADF);
    owner_alive=false;
    io_i2c_release_owner(1);owner_alive=true;assert(releases==2&&!pins_owned[SDA]&&!pins_owned[SCL]);
    c.transfer_timeout_us=5;
    assert(io_i2c_open(1,&c,&h)==0);reset_trace();
    before=now;
    assert(io_i2c_transfer(1,h,0x50,NULL,0,NULL,0)==ERR_TIMEOUT&&now-before<100);
    trace=false;assert(io_i2c_close(1,h)==0);
    c.sda=31;assert(io_i2c_open(1,&c,&h)==ERR_PERM&&!pins_owned[SCL]);
    c.sda=SDA;c.scl=SDA;assert(io_i2c_open(1,&c,&h)==ERR_INVAL);
    const int pool[4][2]={{1,2},{5,6},{7,8},{9,10}};
    c.frequency_hz=100000;
    for(unsigned i=0;i<4;i++){c.sda=pool[i][0];c.scl=pool[i][1];assert(io_i2c_open(1,&c,&h)==0);}
    c.sda=11;c.scl=12;assert(io_i2c_open(1,&c,&h)==ERR_BUSY&&!pins_owned[11]);
    io_i2c_release_owner(1);
    for(unsigned i=0;i<4;i++)assert(!pins_owned[pool[i][0]]&&!pins_owned[pool[i][1]]);
    c.sda=SDA;c.scl=SCL;
    assert(io_i2c_open(1,&c,&h)==0);
    on_release=die_after_pin_release;
    if(setjmp(killed)==0){io_i2c_close(1,h);assert(!"fault not reached");}
    lock.owner=NULL; /* Scheduler reclaimed the killed task's driver mutex. */
    assert(lookup(1,h)==NULL);
    uint32_t replacement;
    assert(io_i2c_open(2,&c,&replacement)==0);
    unsigned touches_before=pin_touches, releases_before=releases;
    io_i2c_release_owner(1);
    assert(pin_touches==touches_before&&releases==releases_before);
    assert(pins_owned[SDA]==2&&pins_owned[SCL]==2);
    assert(io_i2c_close(2,replacement)==0);
    c.sda=SDA;c.scl=SCL;
    assert(io_i2c_open(1,&c,&h)==0);
    touches_before=pin_touches;
    on_owner_check=close_owner_at_start;
    assert(io_i2c_transfer(1,h,0x50,NULL,0,NULL,0)==ERR_PERM);
    assert(pin_touches==touches_before&&buses[0].cancelled);
    io_i2c_release_owner(1);owner_alive=true;
    c.frequency_hz=0;assert(io_i2c_open(1,&c,&h)==ERR_INVAL);
    puts("PASS SoftI2C repeated START/read/write/ACK/NACK/stretch timeout/cancel/owner/stale handle/reserved pins");
}

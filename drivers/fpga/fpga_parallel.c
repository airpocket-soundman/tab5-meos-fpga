#include "fpga_parallel.h"
#include <m5os/hal_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <string.h>
static const int pins[] = {18, 19, 5,  3,  4,  2, 48, 47,
                           16, 51, 45, 17, 52, 6, 7};
enum { CLK = 16, DIR = 51, READY = 45 };
static int translate(int e) {
  return !e                          ? 0
         : e == ERR_TIMEOUT          ? FPGA_LINK_TIMEOUT
         : e == HAL_PARLIO_CANCELLED ? FPGA_LINK_CANCELLED
         : e == ERR_BUSY             ? FPGA_LINK_BUSY
         : e == ERR_NOSYS            ? FPGA_LINK_UNSUPPORTED
                                     : FPGA_LINK_IO;
}
static bool cancelled(void *p) {
  fpga_parallel_t *b = p;
  return b->cancel && b->cancel(b->cancel_context);
}
static int check(fpga_parallel_t *b, uint64_t end) {
  if (cancelled(b))
    return FPGA_LINK_CANCELLED;
  return hal_time_now_us() >= end ? FPGA_LINK_TIMEOUT : 0;
}
static int config(fpga_parallel_t *b, int p, bool out, int value) {
  return io_gpio_config(b->owner, p, out, value, IO_PULL_NONE) ? FPGA_LINK_IO
                                                               : 0;
}
static int set(fpga_parallel_t *b, int p, int v) {
  return io_gpio_write(b->owner, p, v) ? FPGA_LINK_IO : 0;
}
static int inputs(fpga_parallel_t *b) {
  int e = 0;
  for (unsigned i = 0; i < 8; i++)
    if (config(b, pins[i], false, 0))
      e = FPGA_LINK_IO;
  return e;
}
static int safe(fpga_parallel_t *b) {
  if (!b->claimed)
    return 0;
  int e = inputs(b);
  /* Always attempt every cleanup action, even after the first failure. */
  io_gpio_write(b->owner, CLK, 0);
  io_gpio_write(b->owner, DIR, 0);
  for (unsigned i = 8; i < 15; i++)
    if (config(b, pins[i], false, 0))
      e = FPGA_LINK_IO;
  return e;
}
void fpga_parallel_close(fpga_parallel_t *b) {
  if (!b)
    return;
  if (b->native) {
    if(b->stream)hal_parlio_stream_status(b->native,&b->stream_stats);
    hal_parlio_get_stats(b->native, &b->stats);
    if (!hal_parlio_close(b->native)) {
      /* Hardware may still own DMA/ETM. Preserve both leases, even if the
       * caller task dies; automatic IO reaping must not reassign GPIO17. */
      hal_parlio_get_stats(b->native, &b->stats);
      safe(b);
      b->failed = true;
      if (!b->error)b->error = FPGA_LINK_BUSY;
      return;
    }
    b->native = NULL;
  }
  /* The ISR callback retains this pointer until HAL teardown proves quiet. */
  kfree(b->stream);b->stream=NULL;
  int cleanup=safe(b);
  if (b->owner_guarded) {
    if (cleanup || io_owner_protect(b->owner,false)) {
      b->failed=true;
      if(!b->error)b->error=cleanup?cleanup:FPGA_LINK_IO;
      return;
    }
    b->owner_guarded=false;
  }
  if (b->owner)
    io_owner_close(b->owner);
  b->owner = 0;
  b->claimed = false;
}
int fpga_parallel_continuous(fpga_parallel_t *b) {
  if(!b||!b->native||!b->claimed||b->failed)return FPGA_LINK_INVALID;
  if(b->busy||b->stream)return FPGA_LINK_BUSY;
  b->stream=kmalloc(sizeof *b->stream);
  if(!b->stream)return FPGA_LINK_IO;
  fpga_stream_reset(b->stream,0);
  b->native_rx=false;
  return 0;
}
int fpga_parallel_open(fpga_parallel_t *b, const char *profile, uint32_t hz) {
  if (!b || !profile || strcmp(profile, "dock-jumper-v0") ||
      !M5OS_FPGA_DOCK_VERIFIED)
    return FPGA_LINK_INVALID;
  if (b->owner || b->native)
    return FPGA_LINK_BUSY;
  if (hz != 500000 && hz != 1000000 && hz != 2000000 && hz != 5000000)
    return FPGA_LINK_INVALID;
  memset(b, 0, sizeof *b);
  if (io_owner_create("fpga-parlio", &b->owner))
    return FPGA_LINK_BUSY;
  if (io_pins_claim(b->owner, pins, 15, IO_USE_DIGITAL)) {
    fpga_parallel_close(b);
    return FPGA_LINK_BUSY;
  }
  b->claimed = true;
  if (io_owner_protect(b->owner,true)) {
    fpga_parallel_close(b);
    return FPGA_LINK_IO;
  }
  b->owner_guarded=true;
  for (unsigned i = 0; i < 15; i++)
    if (config(b, pins[i], false, 0)) {
      fpga_parallel_close(b);
      return FPGA_LINK_IO;
    }
  int e = translate(hal_parlio_open(hz, &b->native));
  if (e) {
    fpga_parallel_close(b);
    return e;
  }
  return 0;
}
static int pause(fpga_parallel_t *b, uint64_t end, unsigned us) {
  int e = check(b, end);
  if (e)
    return e;
  hal_delay_us(us);
  return check(b, end);
}
static int ready(fpga_parallel_t *b, uint64_t end, int expected) {
  for (unsigned i = 0; i < 1024; i++) {
    int e = check(b, end), v;
    if (e)
      return e;
    if (io_gpio_read(b->owner, READY, &v))
      return FPGA_LINK_IO;
    if (v == expected)
      return 0;
    if ((e = pause(b, end, 100)))
      return e;
  }
  return FPGA_LINK_TIMEOUT;
}
static bool publish(void *p,const uint8_t *data,size_t n,uint32_t offset) {
  return fpga_stream_publish(p,offset,data,n);
}
static int stream_receive(fpga_parallel_t *b,const uint8_t *tx,uint8_t *rx,
                          size_t *actual,uint64_t end) {
  int e=fpga_stream_begin(b->stream,tx)==LINK_STREAM_NEED_MORE?0:FPGA_LINK_DATA;
  if(!e)e=translate(hal_parlio_stream_begin(b->native,publish,b->stream,end));
  for(unsigned spins=0;!e;spins++) {
    link_stream_result r=fpga_stream_poll(b->stream);
    if(link_stream_failed(r)){e=FPGA_LINK_DATA;break;}
    hal_parlio_stream_status(b->native,&b->stream_stats);
    if(b->stream_stats.fatal){e=FPGA_LINK_IO;break;}
    if(b->stream_stats.closed)break;
    e=check(b,end);
    if(!e&&spins>=1000000)e=FPGA_LINK_TIMEOUT;
    if(!e)hal_delay_us(1);
  }
  /* Even failed validation must close sampling and drain callback publication.
   * The queue remains alive until the later full HAL close proves quiescence. */
  int paused=translate(hal_parlio_stream_pause(b->native,end));
  if(!e)e=paused;
  hal_parlio_stream_status(b->native,&b->stream_stats);
  for(unsigned blocks=0;blocks<FPGA_STREAM_STASH/FPGA_STREAM_BLOCK;blocks++) {
    link_stream_result r=fpga_stream_poll(b->stream);
    if(link_stream_failed(r)&&!e)e=FPGA_LINK_DATA;
  }
  if(!e)e=ready(b,end,0);
  bool proof=!e&&b->stream_stats.closed&&!b->stream_stats.fatal&&
             !b->stream_stats.pending&&b->stream_stats.ready_latched;
  link_stream_result final=fpga_stream_finish(b->stream,proof);
  if(!e&&final!=LINK_STREAM_UNIQUE)e=FPGA_LINK_DATA;
  const uint8_t *body=fpga_stream_body(b->stream);
  if(!e&&!body)e=FPGA_LINK_DATA;
  if(!e){memcpy(rx,body,FPGA_WIRE_TILE_BYTES);*actual=FPGA_WIRE_TILE_BYTES;}
  return e;
}
static int exchange(void *p, const uint8_t *tx, size_t n, uint8_t *rx,
                    size_t cap, size_t *received, uint64_t end) {
  fpga_parallel_t *b = p;
  if (received)
    *received = 0;
  if (!b || !tx || !rx || !received || n<8 || n > HAL_PARLIO_MAX_TX || !cap ||
      cap > FPGA_WIRE_CAPTURE_MAX)
    return FPGA_LINK_INVALID;
  if (b->busy)
    return FPGA_LINK_BUSY;
  if (!b->claimed || !b->native || b->failed)
    return FPGA_LINK_IO;
  b->busy = true;
  bool bulk=b->stream&&cap==FPGA_WIRE_TILE_BYTES&&(tx[1]&127)==0x3b&&(tx[1]&128);
  b->attempted++;
  int e = check(b, end);
  size_t actual = 0;
  if (!e)
    e = inputs(b);
  if (!e)
    e = config(b, CLK, true, 0);
  if (!e)
    e = config(b, DIR, true, 0);
  if (!e)
    e = pause(b, end, 200);
  if (!e)
    e = ready(b, end, 0);
  for (unsigned i = 0; !e && i < 8; i++)
    e = config(b, pins[i], true, 0);
  if (!e)
    e = translate(hal_parlio_tx(b->native, tx, n, end, cancelled, b));
  if (!e)
    e = inputs(b);
  if(bulk){
    /* Exact reference dock_gpu.cpp start(): completed reply READY is observed
     * while DIR remains LOW, then preload idle-HIGH CLK before raising DIR.
     * Neither that CLK transition nor an early DIR transition may advance an
     * unfinished FPGA reply. These are explicit2us reference handoff guards. */
    if(!e)e=ready(b,end,1);
    if(!e)e=config(b,CLK,true,1);
    if(!e)e=pause(b,end,2);
    if(!e)e=set(b,DIR,1);
    if(!e)e=pause(b,end,2);
  } else {
    /* Retain finite/GPIO comparison timing, including conservative software
     * receive loops below; this is not full reference performance parity. */
    if (!e)e = config(b, CLK, true, b->native_rx ? 1 : 0);
    if (!e)e = pause(b, end, 200);
    if (!e)e = set(b, DIR, 1);
    if (!e)e = pause(b, end, 200);
    if (!e)e = ready(b, end, 1);
  }
  if(!e&&bulk)e=stream_receive(b,tx,rx,&actual,end);
  else if (!e && b->native_rx)
    e = config(b, 17, true,
               0); /* Claimed AUX0: internal hardware VALID gate. */
  if (!e && !bulk && b->native_rx)
    e = translate(
        hal_parlio_rx(b->native, rx, cap, &actual, end, cancelled, b));
  else if (!e&&!bulk) {
    /* Explicit hybrid mode: transmit DMA, GPIO-controlled finite receive. */
    for (size_t j = 0; !e && j < cap; j++) {
      e = pause(b, end, 100);
      unsigned v = 0;
      for (unsigned i = 0; !e && i < 8; i++) {
        int bit;
        if (io_gpio_read(b->owner, pins[i], &bit))
          e = FPGA_LINK_IO;
        else
          v |= (unsigned)bit << i;
      }
      if (!e) {
        rx[j] = (uint8_t)v;
        actual++;
        e = set(b, CLK, 1);
      }
      if (!e)
        e = pause(b, end, 100);
      if (!e)
        e = set(b, CLK, 0);
      if (!e)
        e = pause(b, end, 100);
    }
  }
  if (!e)
    e = config(b, CLK, true, 0);
  if (!e)
    e = set(b, DIR, 0);
  if (!e)
    e = pause(b, end, 200);
  if (!e)
    e = ready(b, end, 0);
  int cleanup = safe(b);
  if (!e)
    e = cleanup;
  hal_parlio_get_stats(b->native, &b->stats);
  if (e) {
    b->failed = true;
    b->error = e;
  } else {
    b->completed++;
    *received = actual;
  }
  b->busy = false;
  return e;
}
static uint64_t now(void *p) {
  (void)p;
  return hal_time_now_us();
}
const fpga_wire_ops_t fpga_parallel_wire_ops = {exchange, now, cancelled,false};
static int stream_exchange(void *p,const uint8_t *tx,size_t n,uint8_t *rx,
                           size_t cap,size_t *received,uint64_t end) {
  fpga_parallel_t *b=p;
  if(!b||!b->stream||!received||!tx||!rx||n<8)return FPGA_LINK_INVALID;
  *received=0;
  if(cap==FPGA_WIRE_TILE_BYTES){
    if((tx[1]&127)!=0x3b||!(tx[1]&128)||n!=8)return FPGA_LINK_UNSUPPORTED;
    return exchange(p,tx,n,rx,cap,received,end);
  }
  /* Short replies remain the reference GPIO path. Normalize only bytes that
   * the original strict finite validator actually accepted, never synthesize. */
  if(cap!=8)return FPGA_LINK_UNSUPPORTED;
  uint8_t raw[24],body[8];size_t count=0;unsigned offset=0;
  int e=exchange(p,tx,n,raw,(tx[1]&128)?24:8,&count,end);
  if(!e)e=fpga_wire_validate(tx,raw,count,body,8,&offset);
  if(e){b->failed=true;if(!b->error)b->error=e;}
  if(!e){memcpy(rx,body,8);*received=8;}
  return e;
}
const fpga_wire_ops_t fpga_parallel_stream_wire_ops={stream_exchange,now,cancelled,true};
static int begin(void *p, uint64_t t) {
  fpga_parallel_t *b = p;
  b->pending_length = 0;
  return b->failed || !b->claimed ? -1 : check(b, t);
}
static int send(void *p, const uint8_t *d, size_t n, uint64_t t) {
  fpga_parallel_t *b = p;
  if (check(b, t) || n != 8)
    return -1;
  memcpy(b->pending, d, n);
  b->pending_length = n;
  return (int)n;
}
static int turn(void *p, uint64_t t) { return check(p, t); }
static int receive(void *p, uint8_t *d, size_t n, uint64_t t) {
  fpga_parallel_t *b = p;
  if (n != 8 || b->pending_length != 8)
    return -1;
  uint8_t tx[8], capture[24], body[8];
  size_t got = 0;
  memcpy(tx, b->pending, 8);
  if (b->tagged) {
    tx[1] |= 0x80;
    uint16_t c = fpga_link_crc16(tx, 6);
    tx[6] = (uint8_t)c;
    tx[7] = (uint8_t)(c >> 8);
  }
  size_t cap = b->tagged ? 24 : 8;
  int e = exchange(b, tx, 8, capture, cap, &got, t);
  unsigned offset = 0;
  if (!e)
    e = fpga_wire_validate(tx, capture, got, body, 8, &offset);
  if (e) {
    b->error = e;
    b->failed = true;
    return -1;
  }
  memcpy(d, body, 8);
  return 8;
}
static void finish(void *p, bool failed) {
  fpga_parallel_t *b = p;
  if (failed)
    b->failed = true;
  safe(b);
}
static void idle(void *p) {
  (void)p;
  hal_delay_us(100);
}
const fpga_link_ops_t fpga_parallel_link_ops = {begin,  send, turn, receive,
                                                finish, now,  idle, cancelled};

/* Native Dock PARLIO/GDMA, finite and scaled continuous RX. No ESP-IDF peripheral driver/FreeRTOS calls.
 * Register sequence follows Espressif IDF5.5.2 and the validated GPU host;
 * local IDF5.4.4 phase enums differ, so phase bits are explicit/read back.
 * Pins and protocol are owned by tab5-tang-primer20k-carrier. */
#if M5OS_MODULE_FPGA_PARLIO && M5OS_BOARD_TAB5
#include <m5os/hal_parlio.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_io_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/errno.h>
#include <m5os/arch.h>
#include <m5os/mutex.h>
#include <string.h>
#include "esp_cache.h"
#include "esp_intr_alloc.h"
#include "esp_rom_gpio.h"
#include "hal/axi_dma_ll.h"
#include "hal/parlio_ll.h"
#include "hal/gpio_ll.h"
#include "hal/dma_types.h"
#include "hal/etm_ll.h"
#include "hal/gpio_etm_ll.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_sig_map.h"
#include "soc/parlio_periph.h"
#include "soc/axi_dma_struct.h"
#include "soc/parl_io_struct.h"
#include "soc/hp_sys_clkrst_struct.h"
#include "soc/soc_caps.h"
#include "soc/soc_etm_struct.h"
#include "soc/gpio_ext_struct.h"
#include "soc/soc_etm_source.h"
#include "soc/interrupts.h"

_Static_assert(SOC_PARLIO_GROUPS==1&&SOC_PARLIO_TX_UNITS_PER_GROUP==1&&SOC_PARLIO_RX_UNITS_PER_GROUP==1,
               "Retest native PARLIO ownership for this target");
_Static_assert(PARLIO_LL_TX_DATA_LINE_AS_VALID_SIG==15,
               "Native TX16 generated VALID requires the reviewed pre-rev3 layout");

enum { CLOCK_PIN=16, DIR_PIN=51, READY_PIN=45, GATE_PIN=17, DMA_CAPACITY=2688,
       DMA_ALIGN=128, POLL_LIMIT=500000 };
static const int data_pins[8]={18,19,5,3,4,2,48,47};
struct native_stream;
struct hal_parlio {
    uint32_t hz,rx_hz;
    int tx_channel,rx_channel;
    uint8_t *allocation,*payload;
    dma_descriptor_align8_t *desc;
    bool busy,failed,axi_was_off,tx_started,rx_started,dma_quarantined;
    bool etm_was_off,gate_armed;
    int etm_channel,gpio_task_channel;
    uint32_t gate_task_saved;
    hal_parlio_stats_t stats;
    struct native_stream *stream;
    bool drive_saved;
    gpio_drive_cap_t drive_previous[10];
};
/* Electrical parity with tab5-fpga-gpu7a4f83e main/dock_gpu.cpp261-262:
 * DATA[8], CLK and DIR all use GPIO_DRIVE_CAP_0. Caller owns these IO pins.
 * Capture every old setting before any write; quarantine keeps our settings
 * until full HAL teardown proves that DMA cannot drive the bus anymore. */
static int drive_pin(unsigned i)
{
    return i<8?data_pins[i]:i==8?CLOCK_PIN:DIR_PIN;
}
static bool apply_drive(hal_parlio_t *c)
{
    if(!c->drive_saved){
        for(unsigned i=0;i<10;i++)gpio_ll_get_drive_capability(&GPIO,drive_pin(i),&c->drive_previous[i]);
        c->drive_saved=true;
    }
    for(unsigned i=0;i<10;i++)gpio_ll_set_drive_capability(&GPIO,drive_pin(i),GPIO_DRIVE_CAP_0);
    for(unsigned i=0;i<10;i++){
        gpio_drive_cap_t actual;
        gpio_ll_get_drive_capability(&GPIO,drive_pin(i),&actual);
        if(actual!=GPIO_DRIVE_CAP_0)return false;
    }
    return true;
}
static void restore_drive(hal_parlio_t *c)
{
    if(!c->drive_saved)return;
    for(unsigned i=0;i<10;i++)gpio_ll_set_drive_capability(&GPIO,drive_pin(i),c->drive_previous[i]);
    c->drive_saved=false;
}
static mutex_t resource_lock=MUTEX_INIT;
static hal_parlio_t *native_owner;
static void rx_gate_disarm(hal_parlio_t *c);
static bool stream_safe_tx(hal_parlio_t *c);

static int check(uint64_t deadline,hal_parlio_cancel_t cancel,void *arg)
{
    if(cancel&&cancel(arg))return HAL_PARLIO_CANCELLED;
    return hal_time_now_us()>=deadline?ERR_TIMEOUT:0;
}
static int sync_c2m(void *p,size_t n)
{
    return esp_cache_msync(p,(n+127u)&~127u,ESP_CACHE_MSYNC_FLAG_DIR_C2M)==ESP_OK?0:ERR_IO;
}
static int sync_m2c(void *p,size_t n)
{
    return esp_cache_msync(p,(n+127u)&~127u,ESP_CACHE_MSYNC_FLAG_DIR_M2C)==ESP_OK?0:ERR_IO;
}
static void restore_bus(bool clock_output)
{
    for(unsigned i=0;i<8;i++){
        int pin=data_pins[i];gpio_ll_output_disable(&GPIO,pin);
        /* Peripheral OE must not override the disabled GPIO latch. */
        GPIO.func_out_sel_cfg[pin].out_sel=SIG_GPIO_OUT_IDX;
        GPIO.func_out_sel_cfg[pin].out_inv_sel=0;
        GPIO.func_out_sel_cfg[pin].oen_sel=1;
        GPIO.func_out_sel_cfg[pin].oen_inv_sel=0;
        hal_io_gpio_input(pin,0);
    }
    /* Preload the low latch before mux handoff. DIR remains caller-owned. */
    gpio_ll_output_disable(&GPIO,CLOCK_PIN);
    gpio_ll_set_level(&GPIO,CLOCK_PIN,0);
    esp_rom_gpio_connect_out_signal(CLOCK_PIN,SIG_GPIO_OUT_IDX,false,false);
    if(clock_output)hal_io_gpio_output(CLOCK_PIN,0);
    else hal_io_gpio_input(CLOCK_PIN,0);
    gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
}
static unsigned gate_event_id(const hal_parlio_t *c)
{
    return PDMA_AXI_EVT_IN_SUC_EOF_CH0+(unsigned)c->rx_channel;
}
static unsigned gate_task_id(const hal_parlio_t *c)
{
    return GPIO_LL_ETM_TASK_ID_CLR((unsigned)c->gpio_task_channel);
}
static volatile uint32_t *gate_status_reg(bool task,unsigned id,bool clear)
{
    /* ETM route IDs are ONE based. Generated status bit0 represents ID1.
     * P4 status and W1C pairs are interleaved at eight-byte stride. */
    uintptr_t base=(uintptr_t)&SOC_ETM+(task?offsetof(soc_etm_dev_t,task_st0):offsetof(soc_etm_dev_t,evt_st0));
    return (volatile uint32_t *)(base+((id-1u)/32u)*8u+(clear?4u:0u));
}
static uint32_t gate_status_mask(unsigned id){return 1u<<((id-1u)%32u);}
_Static_assert(offsetof(soc_etm_dev_t,evt_st1)-offsetof(soc_etm_dev_t,evt_st0)==8&&
               offsetof(soc_etm_dev_t,task_st1)-offsetof(soc_etm_dev_t,task_st0)==8,
               "Retest P4 ETM status register layout");
static bool gpio_task_unused(unsigned task)
{
    for(unsigned pin=0;pin<SOC_GPIO_PIN_COUNT;pin++)
        if(gpio_ll_etm_is_task_gpio_enabled(&GPIO_ETM,pin)&&
           gpio_ll_etm_gpio_get_task_channel(&GPIO_ETM,pin)==task)return false;
    /* A disabled but configured route is another owner's reservation too. */
    for(unsigned ch=0;ch<SOC_ETM_CHANNELS_PER_GROUP;ch++){
        unsigned id=SOC_ETM.channel[ch].tid.task_id;
        if(id==GPIO_LL_ETM_TASK_ID_SET(task)||id==GPIO_LL_ETM_TASK_ID_CLR(task)||
           id==GPIO_LL_ETM_TASK_ID_TOG(task))return false;
    }
    return true;
}
static int reserve_gate(hal_parlio_t *c)
{
    c->etm_channel=c->gpio_task_channel=-1;
    c->etm_was_off=!HP_SYS_CLKRST.soc_clk_ctrl1.reg_etm_sys_clk_en;
    _etm_ll_enable_bus_clock(0,true); /* Under the caller's short IRQ guard. */
    if(gpio_ll_etm_is_task_gpio_enabled(&GPIO_ETM,GATE_PIN))return ERR_BUSY;
    for(int task=GPIO_LL_ETM_TASK_CHANNELS_PER_GROUP-1;task>=0;task--)
        if(gpio_task_unused((unsigned)task)){c->gpio_task_channel=task;break;}
    for(int ch=SOC_ETM_CHANNELS_PER_GROUP-1;ch>=0;ch--)
        if(!etm_ll_is_channel_enabled(&SOC_ETM,(unsigned)ch)&&
           !SOC_ETM.channel[ch].eid.evt_id&&!SOC_ETM.channel[ch].tid.task_id){c->etm_channel=ch;break;}
    if(c->etm_channel<0||c->gpio_task_channel<0)return ERR_BUSY;
    c->gate_task_saved=(GPIO_ETM.etm_task_pn_cfg[GATE_PIN/4].val>>((GATE_PIN%4)*8))&0xffu;
    c->stats.gate_gpio=GATE_PIN;c->stats.etm_channel=c->etm_channel;c->stats.gpio_task_channel=c->gpio_task_channel;
    return 0;
}
static bool rx_gate_ready(hal_parlio_t *c)
{
    bool ids=c->etm_channel>=0&&c->gpio_task_channel>=0;
    c->stats.gate_gpio_enable=GPIO.enable.val;
    c->stats.gate_gpio_mux=GPIO.func_out_sel_cfg[GATE_PIN].val;
    c->stats.gate_gpio_task=GPIO_ETM.etm_task_pn_cfg[GATE_PIN/4].val;
    c->stats.gate_etm_event=ids?SOC_ETM.channel[c->etm_channel].eid.evt_id:0;
    c->stats.gate_etm_task=ids?SOC_ETM.channel[c->etm_channel].tid.task_id:0;
    bool checks[]={c->gate_armed,ids,
        ids&&etm_ll_is_channel_enabled(&SOC_ETM,(unsigned)c->etm_channel),
        ids&&c->stats.gate_etm_event==gate_event_id(c),
        ids&&c->stats.gate_etm_task==gate_task_id(c),
        gpio_ll_etm_is_task_gpio_enabled(&GPIO_ETM,GATE_PIN),
        ids&&gpio_ll_etm_gpio_get_task_channel(&GPIO_ETM,GATE_PIN)==(unsigned)c->gpio_task_channel,
        (GPIO.enable.val&(1u<<GATE_PIN))!=0,GPIO.func_out_sel_cfg[GATE_PIN].oen_sel!=0,
        GPIO.func_out_sel_cfg[GATE_PIN].out_sel==SIG_GPIO_OUT_IDX,!hal_gpio_read(GATE_PIN)};
    static const char *const stages[]={"rx_gate_not_armed","rx_gate_bad_ids","rx_gate_etm_disabled",
        "rx_gate_event_route","rx_gate_task_route","rx_gate_gpio_disabled","rx_gate_gpio_channel",
        "rx_gate_gpio_output","rx_gate_gpio_oe","rx_gate_gpio_mux","rx_gate_gpio_high"};
    uint32_t mask=0;unsigned first=11;
    for(unsigned i=0;i<11;i++){if(checks[i])mask|=1u<<i;else if(first==11)first=i;}
    c->stats.gate_ready_mask=mask;
    bool ok=first==11;
    if(!ok)c->stats.stage=stages[first];
    c->stats.rx_gate_verified=ok;return ok;
}
static int rx_gate_arm(hal_parlio_t *c)
{
    if(c->etm_channel<0||c->gpio_task_channel<0)return ERR_BUSY;
    /* IO service must have explicitly configured owned, unwired AUX0 as LOW. */
    if(!(GPIO.enable.val&(1u<<GATE_PIN))||hal_gpio_read(GATE_PIN))return ERR_BUSY;
    uint32_t flags=arch_irq_disable();
    etm_ll_disable_channel(&SOC_ETM,(unsigned)c->etm_channel);
    gpio_ll_etm_enable_task_gpio(&GPIO_ETM,GATE_PIN,false);
    gpio_ll_set_level(&GPIO,GATE_PIN,0);
    /* ROM GPIO routing may select peripheral OE even for SIG_GPIO_OUT_IDX.
     * The IO service already owns/enables this LOW pad; make that latch's
     * output-enable authoritative before allowing the hardware CLR task. */
    gpio_ll_set_output_enable_ctrl(&GPIO,GATE_PIN,false,false);
    etm_ll_channel_set_event(&SOC_ETM,(unsigned)c->etm_channel,gate_event_id(c));
    etm_ll_channel_set_task(&SOC_ETM,(unsigned)c->etm_channel,gate_task_id(c));
    gpio_ll_etm_gpio_set_task_channel(&GPIO_ETM,GATE_PIN,(unsigned)c->gpio_task_channel);
    *gate_status_reg(false,gate_event_id(c),true)=gate_status_mask(gate_event_id(c));
    *gate_status_reg(true,gate_task_id(c),true)=gate_status_mask(gate_task_id(c));
    gpio_ll_etm_enable_task_gpio(&GPIO_ETM,GATE_PIN,true);
    etm_ll_enable_channel(&SOC_ETM,(unsigned)c->etm_channel);
    __sync_synchronize();c->gate_armed=true;
    c->stats.rx_gate_closed=false;c->stats.gate_event_seen=false;c->stats.gate_task_seen=false;
    arch_irq_restore(flags);
    if(!rx_gate_ready(c)){rx_gate_disarm(c);return ERR_IO;}
    return 0;
}
static bool rx_gate_closed(hal_parlio_t *c)
{
    if(!c->gate_armed)return false;
    c->stats.gate_event_seen=(*gate_status_reg(false,gate_event_id(c),false)&gate_status_mask(gate_event_id(c)))!=0;
    c->stats.gate_task_seen=(*gate_status_reg(true,gate_task_id(c),false)&gate_status_mask(gate_task_id(c)))!=0;
    c->stats.rx_gate_closed=!hal_gpio_read(GATE_PIN)&&c->stats.gate_event_seen&&c->stats.gate_task_seen;
    return c->stats.rx_gate_closed;
}
static void rx_gate_disarm(hal_parlio_t *c)
{
    uint32_t flags=arch_irq_disable();
    if(c->etm_channel>=0)etm_ll_disable_channel(&SOC_ETM,(unsigned)c->etm_channel);
    if(c->gpio_task_channel>=0)gpio_ll_etm_enable_task_gpio(&GPIO_ETM,GATE_PIN,false);
    /* Disable both event consumers before the pin can be released/reassigned. */
    __sync_synchronize();gpio_ll_set_level(&GPIO,GATE_PIN,0);
    hal_io_gpio_input(GATE_PIN,0);gpio_ll_set_output_enable_ctrl(&GPIO,GATE_PIN,false,false);
    c->gate_armed=false;arch_irq_restore(flags);
}
static void release_gate(hal_parlio_t *c)
{
    rx_gate_disarm(c);
    uint32_t flags=arch_irq_disable();
    if(c->etm_channel>=0){
        etm_ll_channel_set_event(&SOC_ETM,(unsigned)c->etm_channel,0);
        etm_ll_channel_set_task(&SOC_ETM,(unsigned)c->etm_channel,0);
    }
    uint32_t shift=(GATE_PIN%4)*8u;
    uint32_t value=GPIO_ETM.etm_task_pn_cfg[GATE_PIN/4].val;
    GPIO_ETM.etm_task_pn_cfg[GATE_PIN/4].val=(value&~(0xffu<<shift))|(c->gate_task_saved<<shift);
    arch_irq_restore(flags);
}
static bool pair_unused(unsigned ch,bool initially_off)
{
    const unsigned tx=AXI_DMA.out[ch].conf.out_peri_sel.peri_out_sel_chn;
    const unsigned rx=AXI_DMA.in[ch].conf.in_peri_sel.peri_in_sel_chn;
    return !AXI_DMA.out[ch].intr.ena.val&&!AXI_DMA.in[ch].intr.ena.val&&
        !AXI_DMA.out[ch].conf.out_link2.outlink_addr_chn&&
        !AXI_DMA.in[ch].conf.in_link2.inlink_addr_chn&&
        (tx==0||tx==GDMA_LL_INVALID_PERIPH_ID)&&
        (rx==0||rx==GDMA_LL_INVALID_PERIPH_ID)&&
        (initially_off||(axi_dma_ll_tx_is_desc_fsm_idle(&AXI_DMA,ch)&&axi_dma_ll_rx_is_desc_fsm_idle(&AXI_DMA,ch)));
}
static void rcc_tx_clock(bool on)
{
    uint32_t flags=arch_irq_disable();_parlio_ll_tx_enable_clock(&PARL_IO,on);arch_irq_restore(flags);
}
static void rcc_tx_reset(void)
{
    uint32_t flags=arch_irq_disable();_parlio_ll_tx_reset_clock(&PARL_IO);arch_irq_restore(flags);
}
static bool tx_phase_ok(void)
{
    return PARL_IO.tx_clk_cfg.tx_clk_i_inv==1&&PARL_IO.tx_clk_cfg.tx_clk_o_inv==1&&
        !GPIO.func_out_sel_cfg[CLOCK_PIN].out_inv_sel&&
        GPIO.func_out_sel_cfg[CLOCK_PIN].out_sel==(unsigned)parlio_periph_signals.groups[0].tx_units[0].clk_out_sig;
}
static int stop_dma(hal_parlio_t *c,bool rx)
{
    int ch=rx?c->rx_channel:c->tx_channel;
    bool started=rx?c->rx_started:c->tx_started;
    if(!started)return 0;
    if(rx){axi_dma_ll_rx_abort(&AXI_DMA,ch,true);axi_dma_ll_rx_stop(&AXI_DMA,ch);}
    else{axi_dma_ll_tx_abort(&AXI_DMA,ch,true);axi_dma_ll_tx_stop(&AXI_DMA,ch);}
    uint64_t until=hal_time_now_us()+10000;bool quiet=false;
    for(unsigned polls=0;polls<100000;polls++){
        quiet=rx?(axi_dma_ll_rx_is_desc_fsm_idle(&AXI_DMA,ch)&&axi_dma_ll_rx_is_reset_avail(&AXI_DMA,ch)):
                 (axi_dma_ll_tx_is_desc_fsm_idle(&AXI_DMA,ch)&&axi_dma_ll_tx_is_reset_avail(&AXI_DMA,ch));
        if(quiet||hal_time_now_us()>=until)break;
    }
    if(quiet){
        if(rx){axi_dma_ll_rx_reset_channel(&AXI_DMA,ch);axi_dma_ll_rx_abort(&AXI_DMA,ch,false);c->rx_started=false;}
        else{axi_dma_ll_tx_reset_channel(&AXI_DMA,ch);axi_dma_ll_tx_abort(&AXI_DMA,ch,false);c->tx_started=false;}
        return 0;
    }
    /* Hardware may still own descriptors: keep storage/resource quarantined. */
    rx_gate_disarm(c);
    c->dma_quarantined=true;c->stats.dma_quarantined=true;
    c->failed=true;c->stats.stage="dma_quarantined";
    return ERR_TIMEOUT;
}
int hal_parlio_open(uint32_t hz,hal_parlio_t **out)
{
    if(!out||(hz!=500000&&hz!=1000000&&hz!=2000000&&hz!=5000000))return ERR_INVAL;
    *out=NULL;
    if(!mutex_trylock(&resource_lock))return ERR_BUSY;
    if(native_owner||HP_SYS_CLKRST.soc_clk_ctrl1.reg_parlio_sys_clk_en||
       HP_SYS_CLKRST.soc_clk_ctrl2.reg_parlio_apb_clk_en){mutex_unlock(&resource_lock);return ERR_BUSY;}
    size_t bytes=sizeof(hal_parlio_t)+2*DMA_ALIGN+DMA_CAPACITY;
    uint8_t *allocation=kmalloc_internal(bytes);
    if(!allocation){mutex_unlock(&resource_lock);return ERR_NOMEM;}
    if((uintptr_t)allocation<0x4fc00000u||(uintptr_t)allocation+bytes>0x4ffc0000u){kfree(allocation);mutex_unlock(&resource_lock);return ERR_NOMEM;}
    memset(allocation,0,bytes);
    hal_parlio_t *c=(hal_parlio_t *)allocation;
    c->allocation=allocation;c->hz=hz;c->rx_hz=hz;c->tx_channel=c->rx_channel=-1;
    uintptr_t aligned=((uintptr_t)(allocation+sizeof *c)+127u)&~(uintptr_t)127u;
    c->desc=(dma_descriptor_align8_t *)aligned;c->payload=(uint8_t *)(aligned+128u);
    c->axi_was_off=!gdma_ll_is_bus_clock_enabled(1);
    uint32_t flags=arch_irq_disable();
    if(c->axi_was_off){_gdma_ll_enable_bus_clock(1,true);axi_dma_ll_force_enable_reg_clock(&AXI_DMA,true);axi_dma_ll_set_default_memory_range(&AXI_DMA);}
    for(int ch=2;ch>=0;ch--)if(pair_unused((unsigned)ch,c->axi_was_off)){c->tx_channel=c->rx_channel=ch;break;}
    if(c->tx_channel<0){if(c->axi_was_off)_gdma_ll_enable_bus_clock(1,false);arch_irq_restore(flags);kfree(allocation);mutex_unlock(&resource_lock);return ERR_BUSY;}
    int gate_error=reserve_gate(c);
    if(gate_error){
        if(c->etm_was_off)_etm_ll_enable_bus_clock(0,false);
        if(c->axi_was_off)_gdma_ll_enable_bus_clock(1,false);
        arch_irq_restore(flags);kfree(allocation);mutex_unlock(&resource_lock);return gate_error;
    }
    /* No whole AXI group/FSM reset: unrelated channels retain their registers. */
    _parlio_ll_enable_bus_clock(0,true);_parlio_ll_reset_register(0);
    axi_dma_ll_tx_reset_channel(&AXI_DMA,c->tx_channel);
    axi_dma_ll_rx_reset_channel(&AXI_DMA,c->rx_channel);
    _parlio_ll_tx_enable_clock(&PARL_IO,true);
    _parlio_ll_tx_set_clock_source(&PARL_IO,hz==500000?PARLIO_CLK_SRC_XTAL:PARLIO_CLK_SRC_PLL_F160M);
    hal_utils_clk_div_t div={.integer=(hz==500000?40000000u:160000000u)/hz,.numerator=0,.denominator=0};
    _parlio_ll_tx_set_clock_div(&PARL_IO,&div);_parlio_ll_tx_reset_clock(&PARL_IO);
    arch_irq_restore(flags);
    hal_delay_us(2);
    parlio_ll_tx_reset_fifo(&PARL_IO);
    parlio_ll_tx_set_bus_width(&PARL_IO,16);
    /* Rev1/IDF5.4.4 has no valid-delay API; reset defaults are zero delay. */
    parlio_ll_tx_treat_msb_as_valid(&PARL_IO,true);
    parlio_ll_tx_enable_clock_gating(&PARL_IO,true);
    /* IDF5.4.4 enum helper inverts NEG; proven5.5.2 POS uses these bits. */
    PARL_IO.tx_clk_cfg.tx_clk_i_inv=1;PARL_IO.tx_clk_cfg.tx_clk_o_inv=1;
    parlio_ll_enable_interrupt(&PARL_IO,PARLIO_LL_EVENT_TX_MASK|PARLIO_LL_EVENT_RX_MASK,false);
    parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_TX_MASK|PARLIO_LL_EVENT_RX_MASK);
    axi_dma_ll_tx_enable_interrupt(&AXI_DMA,c->tx_channel,GDMA_LL_TX_EVENT_MASK,false);
    axi_dma_ll_tx_clear_interrupt_status(&AXI_DMA,c->tx_channel,GDMA_LL_TX_EVENT_MASK);
    axi_dma_ll_tx_enable_owner_check(&AXI_DMA,c->tx_channel,false);
    axi_dma_ll_tx_enable_auto_write_back(&AXI_DMA,c->tx_channel,false);
    axi_dma_ll_tx_set_eof_mode(&AXI_DMA,c->tx_channel,1);
    axi_dma_ll_tx_enable_descriptor_burst(&AXI_DMA,c->tx_channel,true);
    axi_dma_ll_tx_set_burst_size(&AXI_DMA,c->tx_channel,16);
    axi_dma_ll_tx_connect_to_periph(&AXI_DMA,c->tx_channel,GDMA_TRIG_PERIPH_PARLIO,3);
    c->stats.frequency_hz=hz;c->stats.rx_frequency_hz=hz;
    c->stats.tx_channel=c->tx_channel;c->stats.rx_channel=c->rx_channel;
    bool drive_ok=apply_drive(c);
    c->stats.drive_cap_zero_verified=drive_ok;
    c->stats.stage=drive_ok?"open_unmapped":"drive_cap_readback";
    c->failed=!drive_ok;
    native_owner=c;*out=c;mutex_unlock(&resource_lock);return drive_ok?0:ERR_IO;
}
int hal_parlio_set_rx_frequency(hal_parlio_t *c,uint32_t hz)
{
    if(!c||c!=native_owner)return ERR_INVAL;
    if(hz!=500000&&hz!=1000000&&hz!=2000000&&hz!=5000000&&
       hz!=8000000&&hz!=10000000&&hz!=16000000&&hz!=20000000)return ERR_INVAL;
    /* This API shares the existing caller-serialization contract with TX/RX.
     * Do not alter a failed/quarantined owner or an active descriptor/gate. */
    if(c->busy||c->failed||c->tx_started||c->rx_started||c->gate_armed||c->stream)return ERR_BUSY;
    c->rx_hz=hz;c->stats.rx_frequency_hz=hz;
    return 0;
}
int hal_parlio_tx(hal_parlio_t *c,const uint8_t *src,size_t n,uint64_t deadline,hal_parlio_cancel_t cancel,void *arg)
{
    if(!c||c!=native_owner||!src||n<8||n>HAL_PARLIO_MAX_TX)return ERR_INVAL;
    if(c->busy||c->failed||!stream_safe_tx(c))return ERR_BUSY;
    c->busy=true;c->stats.tx_attempts++;c->stats.stage="tx_precondition";
    c->stats.tx_phase_verified=false;c->stats.parlio_raw=0;c->stats.dma_raw=0;
    uint64_t began=hal_time_now_us();int error=check(deadline,cancel,arg);
    if(!error&&(hal_gpio_read(DIR_PIN)||hal_gpio_read(READY_PIN)||hal_gpio_read(CLOCK_PIN)))error=ERR_BUSY;
    if(!error){
        for(size_t i=0;i<n;i++){c->payload[2*i]=src[i];c->payload[2*i+1]=0;}
        memset(c->desc,0,128);c->desc->dw0.size=2*n;c->desc->dw0.length=2*n;
        c->desc->dw0.suc_eof=1;c->desc->dw0.owner=DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        c->desc->buffer=c->payload;c->desc->next=NULL;
        error=sync_c2m(c->payload,2*n);if(!error)error=sync_c2m(c->desc,128);
    }
    if(!error)error=check(deadline,cancel,arg);
    if(!error){
        c->stats.stage="tx_attach";rcc_tx_clock(true);
        for(unsigned i=0;i<8;i++){
            gpio_ll_input_enable(&GPIO,data_pins[i]);
            esp_rom_gpio_connect_out_signal(data_pins[i],parlio_periph_signals.groups[0].tx_units[0].data_sigs[i],false,false);
            gpio_ll_set_output_enable_ctrl(&GPIO,data_pins[i],false,false);
            gpio_ll_output_enable(&GPIO,data_pins[i]);
        }
        gpio_ll_input_enable(&GPIO,CLOCK_PIN);
        esp_rom_gpio_connect_out_signal(CLOCK_PIN,parlio_periph_signals.groups[0].tx_units[0].clk_out_sig,false,false);
        gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
        gpio_ll_output_enable(&GPIO,CLOCK_PIN);
        c->stats.tx_phase_verified=tx_phase_ok();
        if(!c->stats.tx_phase_verified||hal_gpio_read(CLOCK_PIN))error=ERR_IO;
    }
    if(!error)error=check(deadline,cancel,arg);
    if(!error){
        /* Reproduce proven stock transaction resets after enabled/LOW attach. */
        c->stats.stage="tx_fifo_ready";rcc_tx_reset();
        axi_dma_ll_tx_set_desc_addr(&AXI_DMA,c->tx_channel,(uint32_t)c->desc);
        rcc_tx_clock(false);parlio_ll_tx_reset_fifo(&PARL_IO);
        parlio_ll_tx_set_idle_data_value(&PARL_IO,src[n-1]);
        parlio_ll_tx_set_eof_condition(&PARL_IO,PARLIO_LL_TX_EOF_COND_DATA_LEN);
        parlio_ll_tx_set_trans_bit_len(&PARL_IO,n*16);
        parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_TX_MASK);
        axi_dma_ll_tx_clear_interrupt_status(&AXI_DMA,c->tx_channel,GDMA_LL_TX_EVENT_MASK);
        axi_dma_ll_tx_start(&AXI_DMA,c->tx_channel);
        c->tx_started=true;
        unsigned polls=0;
        while(!parlio_ll_tx_is_ready(&PARL_IO)){
            error=check(deadline,cancel,arg);if(error||++polls>=POLL_LIMIT){if(!error)error=ERR_TIMEOUT;break;}
            if(axi_dma_ll_tx_get_interrupt_status(&AXI_DMA,c->tx_channel,true)&GDMA_LL_EVENT_TX_DESC_ERROR){error=ERR_IO;break;}
        }
    }
    if(!error)error=check(deadline,cancel,arg);
    if(!error&&!tx_phase_ok()){c->stats.tx_phase_verified=false;error=ERR_IO;}
    if(!error){
        c->stats.stage="tx_eof";parlio_ll_tx_start(&PARL_IO,true);rcc_tx_clock(true);
        unsigned polls=0;
        for(;;){
            error=check(deadline,cancel,arg);if(error)break;
            c->stats.parlio_raw=PARL_IO.int_raw.val;
            c->stats.dma_raw=axi_dma_ll_tx_get_interrupt_status(&AXI_DMA,c->tx_channel,true);
            if(c->stats.dma_raw&GDMA_LL_EVENT_TX_DESC_ERROR){error=ERR_IO;break;}
            if((c->stats.parlio_raw&PARLIO_LL_EVENT_TX_FIFO_EMPTY)&&!(c->stats.parlio_raw&PARLIO_LL_EVENT_TX_EOF)){error=ERR_IO;break;}
            if(c->stats.parlio_raw&PARLIO_LL_EVENT_TX_EOF)break;
            error=check(deadline,cancel,arg);if(error||++polls>=POLL_LIMIT){if(!error)error=ERR_TIMEOUT;break;}
        }
    }
    if(!error&&!tx_phase_ok()){c->stats.tx_phase_verified=false;error=ERR_IO;}
    if(!error)hal_delay_us(2);
    rcc_tx_clock(false);parlio_ll_tx_start(&PARL_IO,false);
    int stopped=stop_dma(c,false);if(!error&&stopped)error=stopped;
    parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_TX_MASK);
    restore_bus(true);hal_delay_us(2);
    c->stats.elapsed_us=(uint32_t)(hal_time_now_us()-began);c->stats.last_error=error;
    if(!error){c->stats.stage="tx_complete";c->stats.tx_completed++;c->stats.tx_bytes+=n;}else c->failed=true;
    c->busy=false;return error;
}

/* The separate finite RX helper is included here to share the reserved channel,
 * aligned storage, deadlines and cleanup rather than allocating another owner. */
#include "parlio_native_rx.inc"
#include "parlio_native_stream.inc"

int hal_parlio_rx(hal_parlio_t *c,uint8_t *dst,size_t n,size_t *received,uint64_t deadline,hal_parlio_cancel_t cancel,void *arg)
{
    if(received)*received=0;
    if(!c||c!=native_owner||!dst||!received||!n||n>HAL_PARLIO_MAX_RX)return ERR_INVAL;
    if(c->busy||c->failed||c->stream)return ERR_BUSY;
    c->busy=true;c->stats.rx_attempts++;
    uint64_t began=hal_time_now_us();int error=native_rx(c,dst,n,deadline,cancel,arg);
    c->stats.elapsed_us=(uint32_t)(hal_time_now_us()-began);c->stats.last_error=error;
    if(!error){*received=n;c->stats.rx_completed++;c->stats.rx_bytes+=n;}
    else c->failed=true;
    c->busy=false;return error;
}
void hal_parlio_get_stats(const hal_parlio_t *c,hal_parlio_stats_t *out)
{
    if(out){if(c&&c==native_owner)*out=c->stats;else memset(out,0,sizeof *out);}
}
bool hal_parlio_close(hal_parlio_t *c)
{
    if(!c||c!=native_owner||c->busy)return false;
    if(!hal_parlio_stream_stop(c))return false;
    mutex_lock(&resource_lock);
    rx_gate_disarm(c);
    rcc_tx_clock(false);parlio_ll_tx_start(&PARL_IO,false);
    uint32_t flags=arch_irq_disable();_parlio_ll_rx_enable_clock(&PARL_IO,false);arch_irq_restore(flags);
    parlio_ll_rx_start(&PARL_IO,false);
    int tx_stop=stop_dma(c,false),rx_stop=stop_dma(c,true);
    if(tx_stop||rx_stop||c->dma_quarantined){
        c->failed=true;c->stats.stage="dma_quarantined";restore_bus(false);
        mutex_unlock(&resource_lock);return false;
    }
    axi_dma_ll_tx_reset_channel(&AXI_DMA,c->tx_channel);axi_dma_ll_rx_reset_channel(&AXI_DMA,c->rx_channel);
    axi_dma_ll_tx_disconnect_from_periph(&AXI_DMA,c->tx_channel);axi_dma_ll_rx_disconnect_from_periph(&AXI_DMA,c->rx_channel);
    axi_dma_ll_tx_set_desc_addr(&AXI_DMA,c->tx_channel,0);axi_dma_ll_rx_set_desc_addr(&AXI_DMA,c->rx_channel,0);
    axi_dma_ll_tx_clear_interrupt_status(&AXI_DMA,c->tx_channel,GDMA_LL_TX_EVENT_MASK);
    axi_dma_ll_rx_clear_interrupt_status(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK);
    release_gate(c);
    restore_bus(false);
    restore_drive(c);
    flags=arch_irq_disable();_parlio_ll_enable_bus_clock(0,false);arch_irq_restore(flags);
    /* Leave AXI clock alone: another unrelated channel may now use it. */
    native_owner=NULL;uint8_t *allocation=c->allocation;mutex_unlock(&resource_lock);kfree(allocation);
    return true;
}
#endif

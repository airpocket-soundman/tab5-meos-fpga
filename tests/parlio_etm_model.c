/* Register-level model for actual native ETM gate helper functions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error assertions required
#endif
#define SOC_ETM_CHANNELS_PER_GROUP 50
#define SOC_GPIO_PIN_COUNT 55
#define GPIO_LL_ETM_TASK_CHANNELS_PER_GROUP 8
#define PDMA_AXI_EVT_IN_SUC_EOF_CH0 197
#define GPIO_LL_ETM_TASK_ID_SET(c) (1u+(c))
#define GPIO_LL_ETM_TASK_ID_CLR(c) (9u+(c))
#define GPIO_LL_ETM_TASK_ID_TOG(c) (17u+(c))
#define SIG_GPIO_OUT_IDX 256
#define ERR_BUSY -5
#define ERR_IO -6
#define GATE_PIN 17
struct reg {uint32_t val;};
typedef struct {
 bool enabled[50];
 struct {struct{uint32_t evt_id;}eid;struct{uint32_t task_id;}tid;}channel[50];
 struct reg evt_st0,evt_st0_clr,evt_st1,evt_st1_clr,evt_st2,evt_st2_clr,evt_st3,evt_st3_clr;
 struct reg evt_st4,evt_st4_clr,evt_st5,evt_st5_clr,evt_st6,evt_st6_clr,evt_st7,evt_st7_clr;
 struct reg task_st0,task_st0_clr,task_st1,task_st1_clr,task_st2,task_st2_clr,task_st3,task_st3_clr;
}soc_etm_dev_t;
static soc_etm_dev_t SOC_ETM;
static struct {struct reg etm_task_pn_cfg[14];}GPIO_ETM;
static struct {struct{struct{bool reg_etm_sys_clk_en;}soc_clk_ctrl1;}dummy;}unused;
static struct {struct{bool reg_etm_sys_clk_en;}soc_clk_ctrl1;}HP_SYS_CLKRST;
static struct {struct reg enable,out;union{uint32_t val;struct{unsigned out_sel:9;unsigned reserved:1;unsigned oen_sel:1;};}func_out_sel_cfg[55];}GPIO;
typedef struct {int gate_gpio,etm_channel,gpio_task_channel;bool rx_gate_verified,rx_gate_closed,gate_event_seen,gate_task_seen;uint32_t gate_ready_mask,gate_gpio_enable,gate_gpio_mux,gate_gpio_task,gate_etm_event,gate_etm_task;const char *stage;}hal_parlio_stats_t;
typedef struct {int rx_channel,etm_channel,gpio_task_channel;bool etm_was_off,gate_armed;uint32_t gate_task_saved;hal_parlio_stats_t stats;}hal_parlio_t;
static void rx_gate_disarm(hal_parlio_t*);
static uint32_t arch_irq_disable(void){return 0;}
static void arch_irq_restore(uint32_t f){(void)f;
 for(unsigned task=0;task<2;task++)for(unsigned i=0;i<(task?4u:8u);i++){
  uintptr_t base=(uintptr_t)&SOC_ETM+(task?offsetof(soc_etm_dev_t,task_st0):offsetof(soc_etm_dev_t,evt_st0));
  volatile uint32_t *st=(volatile uint32_t*)(base+i*8u);st[0]&=~st[1];st[1]=0;
 }
}
static void _etm_ll_enable_bus_clock(unsigned g,bool on){assert(g==0);HP_SYS_CLKRST.soc_clk_ctrl1.reg_etm_sys_clk_en=on;}
static bool etm_ll_is_channel_enabled(soc_etm_dev_t*d,unsigned c){return d->enabled[c];}
static void etm_ll_enable_channel(soc_etm_dev_t*d,unsigned c){d->enabled[c]=true;}
static void etm_ll_disable_channel(soc_etm_dev_t*d,unsigned c){d->enabled[c]=false;}
static void etm_ll_channel_set_event(soc_etm_dev_t*d,unsigned c,unsigned id){d->channel[c].eid.evt_id=id;}
static void etm_ll_channel_set_task(soc_etm_dev_t*d,unsigned c,unsigned id){d->channel[c].tid.task_id=id;}
#define GET_TASK_WORD(d,p) ((d)->etm_task_pn_cfg[(p)/4].val)
static bool gpio_ll_etm_is_task_gpio_enabled(void*d,unsigned p){(void)d;return GET_TASK_WORD(&GPIO_ETM,p)&(1u<<((p%4)*8));}
static unsigned gpio_ll_etm_gpio_get_task_channel(void*d,unsigned p){(void)d;return (GET_TASK_WORD(&GPIO_ETM,p)>>((p%4)*8+1))&7;}
static void gpio_ll_etm_enable_task_gpio(void*d,unsigned p,bool on){(void)d;unsigned s=(p%4)*8;GET_TASK_WORD(&GPIO_ETM,p)=(GET_TASK_WORD(&GPIO_ETM,p)&~(1u<<s))|((unsigned)on<<s);}
static void gpio_ll_etm_gpio_set_task_channel(void*d,unsigned p,unsigned ch){(void)d;unsigned s=(p%4)*8+1;GET_TASK_WORD(&GPIO_ETM,p)=(GET_TASK_WORD(&GPIO_ETM,p)&~(7u<<s))|(ch<<s);}
static int hal_gpio_read(int p){return (GPIO.out.val>>p)&1;}
static void gpio_ll_set_level(void*d,int p,int v){(void)d;GPIO.out.val=(GPIO.out.val&~(1u<<p))|((unsigned)v<<p);}
static void hal_io_gpio_input(int p,int pull){assert(pull==0);GPIO.enable.val&=~(1u<<p);}
static bool stuck_oe;
static void gpio_ll_set_output_enable_ctrl(void*d,int p,bool peripheral,bool inv){(void)d;assert(!inv);GPIO.func_out_sel_cfg[p].oen_sel=stuck_oe?0:!peripheral;}
/* ACTUAL_HELPERS */
static void gate_output_low(void){GPIO.enable.val|=1u<<17;GPIO.func_out_sel_cfg[17].oen_sel=1;GPIO.func_out_sel_cfg[17].out_sel=SIG_GPIO_OUT_IDX;gpio_ll_set_level(&GPIO,17,0);}
static void hardware_eof(unsigned id){
 /* Independent generated-register mapping, not the driver's indexing helper.
  * soc_etm_struct.h: AXI RX2 EOF(ID199)=EVT_ST6.bit6;
  * GPIO CLR7(ID16)=TASK_ST0.bit15. */
 assert(id==199);SOC_ETM.evt_st6.val|=1u<<6;
 for(unsigned ch=0;ch<50;ch++)if(SOC_ETM.enabled[ch]&&SOC_ETM.channel[ch].eid.evt_id==id){
  unsigned task=SOC_ETM.channel[ch].tid.task_id;
  for(unsigned pin=0;pin<55;pin++)if(gpio_ll_etm_is_task_gpio_enabled(&GPIO_ETM,pin)&&task==GPIO_LL_ETM_TASK_ID_CLR(gpio_ll_etm_gpio_get_task_channel(&GPIO_ETM,pin)))gpio_ll_set_level(&GPIO,(int)pin,0);
  assert(task>=9&&task<=16);SOC_ETM.task_st0.val|=1u<<(task-1u);
 }
}
static void reset(void){memset(&SOC_ETM,0,sizeof SOC_ETM);memset(&GPIO_ETM,0,sizeof GPIO_ETM);memset(&GPIO,0,sizeof GPIO);memset(&unused,0,sizeof unused);memset(&HP_SYS_CLKRST,0,sizeof HP_SYS_CLKRST);}
int main(void){
 reset();hal_parlio_t c={.rx_channel=2};
 assert(gate_status_reg(false,199,false)==&SOC_ETM.evt_st6.val&&gate_status_mask(199)==(1u<<6));
 assert(gate_status_reg(true,16,false)==&SOC_ETM.task_st0.val&&gate_status_mask(16)==(1u<<15));
 SOC_ETM.channel[49].eid.evt_id=42;SOC_ETM.channel[49].tid.task_id=3;SOC_ETM.enabled[49]=true;
 gpio_ll_etm_gpio_set_task_channel(&GPIO_ETM,18,7);gpio_ll_etm_enable_task_gpio(&GPIO_ETM,18,true);
 gpio_ll_etm_gpio_set_task_channel(&GPIO_ETM,17,3);uint32_t foreign=GPIO_ETM.etm_task_pn_cfg[4].val;
 assert(!reserve_gate(&c));assert(c.etm_channel!=49&&c.gpio_task_channel!=7);
 assert(SOC_ETM.enabled[49]&&SOC_ETM.channel[49].eid.evt_id==42);
 assert(rx_gate_arm(&c)==ERR_BUSY);gate_output_low();GPIO.func_out_sel_cfg[17].oen_sel=0;
 stuck_oe=true;assert(rx_gate_arm(&c)==ERR_IO);assert(!strcmp(c.stats.stage,"rx_gate_gpio_oe"));
 assert(c.stats.gate_ready_mask==(0x7ffu&~(1u<<8)));stuck_oe=false;gate_output_low();
 assert(!rx_gate_arm(&c)&&rx_gate_ready(&c)); /* Normalize ROM peripheral-OE selection. */
 GPIO.func_out_sel_cfg[17].oen_sel=0;assert(!rx_gate_ready(&c));
 assert(!strcmp(c.stats.stage,"rx_gate_gpio_oe"));
 assert(c.stats.gate_ready_mask==(0x7ffu&~(1u<<8)));
 GPIO.func_out_sel_cfg[17].oen_sel=1;assert(rx_gate_ready(&c));
 assert(!rx_gate_closed(&c));gpio_ll_set_level(&GPIO,17,1);assert(!rx_gate_closed(&c));
 gpio_ll_set_level(&GPIO,17,0);assert(!rx_gate_closed(&c)); /* CPU LOW is not proof. */
 gpio_ll_set_level(&GPIO,17,1);hardware_eof(gate_event_id(&c));assert(rx_gate_closed(&c));
 assert(c.stats.gate_event_seen&&c.stats.gate_task_seen&&c.stats.rx_gate_closed);
 rx_gate_disarm(&c);assert(!SOC_ETM.enabled[c.etm_channel]&&!gpio_ll_etm_is_task_gpio_enabled(&GPIO_ETM,17)&&!(GPIO.enable.val&(1u<<17)));
 gate_output_low();gpio_ll_set_level(&GPIO,17,1);hardware_eof(gate_event_id(&c));assert(hal_gpio_read(17)==1); /* Late EOF cannot touch new GPIO owner. */
 release_gate(&c);assert(!SOC_ETM.channel[c.etm_channel].eid.evt_id&&!SOC_ETM.channel[c.etm_channel].tid.task_id);
 assert(GPIO_ETM.etm_task_pn_cfg[4].val==foreign);assert(SOC_ETM.enabled[49]);
 reset();c=(hal_parlio_t){.rx_channel=2};gpio_ll_etm_enable_task_gpio(&GPIO_ETM,17,true);assert(reserve_gate(&c)==ERR_BUSY);
 reset();c=(hal_parlio_t){.rx_channel=2};for(unsigned ch=0;ch<50;ch++)SOC_ETM.channel[ch].eid.evt_id=1;assert(reserve_gate(&c)==ERR_BUSY);
 puts("Native ETM model: resource collision guards, route/readback, hardware EOF/clear proof, CPU-LOW rejection, late-event isolation and unrelated-resource preservation passed");
 return 0;
}

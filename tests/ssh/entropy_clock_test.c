/* Compile the actual entropy sequence. Fake hardware enforces SDK5.5's
 * analog-I2C clock/power preconditions; no entropy quality/hardware claim. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
enum {LPPERI_CLK_EN_REG,HP_SYS_CLKRST_SOC_CLK_CTRL2_REG,HP_SYS_CLKRST_PERI_CLK_CTRL23_REG,
 RTCADC_MEAS1_MUX_REG,PMU_RF_PWC_REG,ADC_CTRL_REG_REG,ADC_CTRL2_REG,ADC_SAR1_PATT_TAB1_REG,
 HP_SYS_CLKRST_PERI_CLK_CTRL24_REG,ADC_INT_RAW_REG,ADC_INT_CLR_REG,REG_COUNT};
#define LPPERI_CK_EN_LP_I2CMST (1u<<2)
#define LPPERI_CK_EN_LP_ADC (1u<<3)
#define HP_SYS_CLKRST_REG_ADC_APB_CLK_EN 1u
#define HP_SYS_CLKRST_REG_ADC_CLK_EN 2u
#define RTCADC_SAR1_DIG_FORCE 1u
#define PMU_XPD_PERIF_I2C (1u<<5)
#define HP_SYS_CLKRST_REG_ADC_SAR1_CLK_DIV_NUM_M 255u
#define HP_SYS_CLKRST_REG_ADC_SAR1_CLK_DIV_NUM_S 0u
#define ADC_START_FORCE 1u
#define ADC_SAR1_PATT_LEN 15u
#define ADC_SAR1_PATT_LEN_S 8u
#define ADC_XPD_SAR1_FORCE 3u
#define ADC_XPD_SAR1_FORCE_S 4u
#define ADC_XPD_SAR2_FORCE 3u
#define ADC_XPD_SAR2_FORCE_S 6u
#define ADC_TIMER_EN 1u
#define ADC_TIMER_TARGET 65535u
#define ADC_TIMER_TARGET_S 8u
#define ADC_SAR1_DONE_INT_RAW 1u
#define ADC_APB_SARADC1_DONE_INT_CLR 1u
#define I2C_SAR_ADC 1u
#define I2C_SAR_ADC_HOSTID 0u
#define ADC_SAR1_INITIAL_CODE_LOW_ADDR 0u
#define ADC_SAR1_INITIAL_CODE_HIGH_ADDR 1u
#define I2C_SAR_ADC_ENT_VDD_GRP1 2u
#define I2C_SAR_ADC_DTEST_VDD_GRP1 3u
#define ADC_SAR1_INITIAL_CODE_LOW_ADDR_MSB 7u
#define ADC_SAR1_INITIAL_CODE_LOW_ADDR_LSB 0u
#define ADC_SAR1_INITIAL_CODE_HIGH_ADDR_MSB 7u
#define ADC_SAR1_INITIAL_CODE_HIGH_ADDR_LSB 0u
#define I2C_SAR_ADC_ENT_VDD_GRP1_MSB 7u
#define I2C_SAR_ADC_ENT_VDD_GRP1_LSB 0u
#define I2C_SAR_ADC_DTEST_VDD_GRP1_MSB 7u
#define I2C_SAR_ADC_DTEST_VDD_GRP1_LSB 0u
static uint32_t regs[REG_COUNT];static uint8_t analog[4];
static unsigned irq,reads,writes,fills;static bool ready;static uint64_t clock_us;
static uint32_t arch_irq_disable(void){unsigned old=irq;irq++;return old;}
static void arch_irq_restore(uint32_t old){assert(irq==old+1);irq=old;}
static uint64_t hal_time_now_us(void){assert(irq);clock_us+=10000;return clock_us;}
static uint32_t reg_read(unsigned reg){assert(irq&&reg<REG_COUNT);return reg==ADC_INT_RAW_REG?(ready?1u:0u):regs[reg];}
static void reg_write(unsigned reg,uint32_t value){assert(irq&&reg<REG_COUNT);regs[reg]=value;}
#define REG_READ(r) reg_read(r)
#define REG_WRITE(r,v) reg_write(r,v)
#define WRITE_PERI_REG(r,v) reg_write(r,v)
#define SET_PERI_REG_MASK(r,m) reg_write(r,reg_read(r)|(m))
#define CLEAR_PERI_REG_MASK(r,m) reg_write(r,reg_read(r)&~(m))
#define GET_PERI_REG_MASK(r,m) (reg_read(r)&(m))
#define GET_PERI_REG_BITS2(r,m,s) ((reg_read(r)&(m))>>(s))
#define SET_PERI_REG_BITS(r,m,v,s) reg_write(r,(reg_read(r)&~((m)<<(s)))|(((v)&(m))<<(s)))
static void analog_access(unsigned block,unsigned host,unsigned reg,unsigned msb,unsigned lsb){
 assert(irq&&block==1&&host==0&&reg<4&&msb==7&&lsb==0);
 assert(regs[LPPERI_CLK_EN_REG]&LPPERI_CK_EN_LP_I2CMST);
 assert(regs[PMU_RF_PWC_REG]&PMU_XPD_PERIF_I2C);
}
static uint8_t esp_rom_regi2c_read_mask(unsigned b,unsigned h,unsigned r,unsigned msb,unsigned lsb){analog_access(b,h,r,msb,lsb);reads++;return analog[r];}
static void esp_rom_regi2c_write_mask(unsigned b,unsigned h,unsigned r,unsigned msb,unsigned lsb,uint8_t v){analog_access(b,h,r,msb,lsb);writes++;analog[r]=v;}
static void esp_fill_random(void*p,size_t n){assert(irq&&ready&&n==32);assert(analog[2]==1&&analog[3]==0);fills++;memset(p,0x5a,n);}
#define MEOS_ENTROPY_HOST_TEST 1
#include "../../user/ssh/ssh_entropy_tab5.inc"
int main(void){
 for(unsigned initial_clock=0;initial_clock<2;initial_clock++)for(unsigned initial_power=0;initial_power<2;initial_power++)for(unsigned success=0;success<2;success++){
  for(unsigned i=0;i<REG_COUNT;i++)regs[i]=0xa5000000u+i;
  regs[LPPERI_CLK_EN_REG]&=~LPPERI_CK_EN_LP_I2CMST;regs[LPPERI_CLK_EN_REG]|=initial_clock?LPPERI_CK_EN_LP_I2CMST:0;
  regs[PMU_RF_PWC_REG]&=~PMU_XPD_PERIF_I2C;regs[PMU_RF_PWC_REG]|=initial_power?PMU_XPD_PERIF_I2C:0;
  for(unsigned i=0;i<4;i++)analog[i]=(uint8_t)(33+i);
  uint32_t saved[8];memcpy(saved,regs,sizeof saved);uint8_t saved_analog[4];memcpy(saved_analog,analog,4);
  uint8_t seed[32];memset(seed,0xcc,32);ready=success;reads=writes=fills=irq=0;clock_us=0;
  assert(ssh_entropy_seed(seed)==(bool)success);
  assert(!irq&&reads==4&&writes>4&&fills==success);
  assert(!memcmp(saved,regs,sizeof saved)&&!memcmp(saved_analog,analog,4));
  for(unsigned i=0;i<32;i++)assert(seed[i]==(success?0x5a:0xcc));
 }
 puts("PASS actual entropy: clock/power before first ROM read, initial-on/off state restored, success/timeout, IRQ scope and no fallback seed");return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../drivers/tab5kbd/tab5touch.c"

static bool fake_st=true, fake_failure;
static uint8_t fake_adv=8,fake_report[70],fake_gt_status=0x81;
static unsigned writes,wake_calls;
void kprintf(const char *fmt,...){(void)fmt;}
void hal_gpio_config_output(int pin,int level){(void)pin;(void)level;wake_calls++;}
void hal_gpio_config_input_pullup(int pin){(void)pin;}
void hal_gpio_write(int pin,int value){(void)pin;(void)value;}
void hal_delay_us(uint32_t us){(void)us;}
bool hal_i2c_write_reg16(uint8_t addr,uint16_t reg,uint8_t value){(void)addr;(void)reg;(void)value;writes++;return true;}
bool hal_i2c_read_reg16(uint8_t addr,uint16_t reg,uint8_t *buf,size_t len)
{
    if(fake_failure)return false;
    memset(buf,0,len);
    if(fake_st&&addr==0x55){
        if(reg==0&&len==1){*buf=1;return true;}
        if(reg==5&&len==5){uint8_t info[]={2,208,5,0,10};memcpy(buf,info,5);return true;}
        if(reg==0x10&&len==1){*buf=fake_adv;return true;}
        if(reg==0x14&&len==70){memcpy(buf,fake_report,70);return true;}
    }
    if(!fake_st&&addr==0x14){
        if(reg==REG_PRODUCT&&len==4){memcpy(buf,"911\0",4);return true;}
        if(reg==REG_XRES&&len==4){uint8_t res[]={0xd0,2,0,5};memcpy(buf,res,4);return true;}
        if(reg==REG_STATUS&&len==1){*buf=fake_gt_status;return true;}
        if(reg==REG_POINT0&&len==6){uint8_t p[]={100,0,0xc8,0,0,0};memcpy(buf,p,6);return true;}
    }
    return false;
}
int main(void)
{
    int x=-1,y=-1;
    assert(tab5_touch_init());assert(s_st7123&&s_max_x==720&&s_max_y==1280&&s_map==1);
    assert(writes==0&&wake_calls==0); /* ST is read-only, without GT911 wake/ack protocol. */
    /* First valid contact may occupy a later report slot. */
    fake_report[14]=0x80;fake_report[15]=100;fake_report[16]=0;fake_report[17]=200;
    assert(tab5_touch_read(&x,&y)&&x==DISPLAY_WIDTH-201&&y==100);
    memset(fake_report,0,sizeof fake_report);assert(!tab5_touch_read(&x,&y));
    fake_adv=0;assert(!tab5_touch_read(&x,&y));assert(s_frames==3&&s_errors==0);
    fake_failure=true;assert(!tab5_touch_read(&x,&y)&&s_errors==1);fake_failure=false;
    /* Fallback remains available for the original panel. */
    fake_st=false;s_present=false;s_st7123=false;assert(tab5_touch_init());assert(!s_st7123&&wake_calls>0);
    assert(tab5_touch_read(&x,&y)&&x==DISPLAY_WIDTH-201&&y==100);
    fake_gt_status=0x80;assert(!tab5_touch_read(&x,&y));assert(writes>=3);
    puts("PASS: ST7123-compatible probe/report/mapping/release/error and GT911 fallback");
    return 0;
}

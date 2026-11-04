/* Native port of dock_dma_rx.cpp scaled_1024x4 and patched parlio_rx.c.
 * Same free-running Core clock / soft delimiter; no GPIO17 VALID or ETM.
 * Task owns session; one real AXI DMA IRQ publishes one completed block.
 * DMA owner checking is OFF exactly as in the reference infinite ring.
 */
enum { STREAM_BLOCK=1024,STREAM_NODES=4,STREAM_RING=4096,STREAM_BUDGET=5120 };
struct native_stream {
    void *allocation;
    dma_descriptor_align8_t *desc[STREAM_NODES];
    uint8_t *payload;
    intr_handle_t irq;
    hal_parlio_stream_block_t callback;
    void *context;
    volatile uint32_t blocks,bytes,epoch_start,eofs,late_bytes,irq_raw;
    volatile uint32_t stop_reason,last_callback_us;
    volatile bool started,active,closed,fatal,ready_latched;
};
static bool stream_safe_tx(hal_parlio_t *c)
{
    return !c->stream||(!c->stream->active&&!c->stream->fatal);
}
static bool stream_phase_clock_ok(hal_parlio_t *c,bool mapped)
{
    bool ok=PARL_IO.rx_clk_cfg.rx_clk_i_inv==0&&PARL_IO.rx_clk_cfg.rx_clk_o_inv==0&&
        HP_SYS_CLKRST.peri_clk_ctrl117.reg_parlio_rx_clk_src_sel==2&&
        HP_SYS_CLKRST.peri_clk_ctrl117.reg_parlio_rx_clk_div_num+1u==160000000u/c->rx_hz&&
        HP_SYS_CLKRST.peri_clk_ctrl118.reg_parlio_rx_clk_div_numerator==0;
    if(mapped)ok=ok&&GPIO.func_out_sel_cfg[CLOCK_PIN].out_sel==(unsigned)parlio_periph_signals.groups[0].rx_units[0].clk_out_sig&&
        GPIO.func_out_sel_cfg[CLOCK_PIN].out_inv_sel;
    c->stats.rx_phase_verified=ok;return ok;
}
static void stream_close_validation(struct native_stream *s,unsigned reason)
{
    /* ISR-safe: leave CLK mapping, RX_START, clock tree and DMA alive. */
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);
    s->active=false;s->stop_reason=reason;__sync_synchronize();s->closed=true;
}
static void native_stream_isr(void *arg)
{
    hal_parlio_t *c=arg;struct native_stream *s=c->stream;
    if(!s)return;
    const uint32_t raw=axi_dma_ll_rx_get_interrupt_status(&AXI_DMA,c->rx_channel,true);
    if(!raw)return;
    axi_dma_ll_rx_clear_interrupt_status(&AXI_DMA,c->rx_channel,raw);
    s->irq_raw=raw;
    if((raw&0x7fcu)||(PARL_IO.int_raw.val&PARLIO_LL_EVENT_RX_FIFO_FULL)) {
        s->fatal=true;stream_close_validation(s,3);return;
    }
    if(raw&2u)s->eofs++;
    if(!(raw&1u)||s->fatal)return; /* RX_DONE per descriptor, EOF per ring. */
    unsigned index=(s->bytes/STREAM_BLOCK)%STREAM_NODES;
    if((raw&2u)&&index!=STREAM_NODES-1u) {
        s->fatal=true;stream_close_validation(s,4);return;
    }
    uint32_t now=(uint32_t)hal_time_now_us();
    if(s->active&&s->last_callback_us&&
       (uint32_t)(now-s->last_callback_us)>(uint64_t)(STREAM_RING-STREAM_BLOCK)*1000000u/c->rx_hz) {
        s->fatal=true;stream_close_validation(s,5);return;
    }
    s->last_callback_us=now;
    dma_descriptor_align8_t *d=s->desc[index];
    if(sync_m2c(d,128)||d->buffer!=s->payload+index*STREAM_BLOCK||
       d->dw0.size!=STREAM_BLOCK||d->dw0.length!=STREAM_BLOCK||
       d->next!=s->desc[(index+1u)%STREAM_NODES]||
       sync_m2c(d->buffer,STREAM_BLOCK)) {
        s->fatal=true;stream_close_validation(s,6);return;
    }
    if(!s->callback(s->context,d->buffer,STREAM_BLOCK,s->bytes)) {
        s->fatal=true;stream_close_validation(s,7);return;
    }
    s->blocks++;s->bytes+=STREAM_BLOCK;__sync_synchronize();
    if(s->closed)s->late_bytes+=STREAM_BLOCK;
    else if(s->active) {
        uint32_t epoch=s->bytes-s->epoch_start;
        if(epoch>=STREAM_BUDGET)stream_close_validation(s,2);
        else if(s->ready_latched)stream_close_validation(s,1);
        else if(!gpio_ll_get_level(&GPIO,READY_PIN))s->ready_latched=true;
    }
}
static void stream_clock_gpio_low(void)
{
    gpio_ll_set_level(&GPIO,CLOCK_PIN,0);
    esp_rom_gpio_connect_out_signal(CLOCK_PIN,SIG_GPIO_OUT_IDX,false,false);
    gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
    gpio_ll_output_enable(&GPIO,CLOCK_PIN);
}
static int stream_allocate(hal_parlio_t *c)
{
    size_t bytes=sizeof(struct native_stream)+DMA_ALIGN+STREAM_NODES*DMA_ALIGN+STREAM_RING;
    void *allocation=kmalloc_internal(bytes);
    if(!allocation)return ERR_NOMEM;
    if((uintptr_t)allocation<0x4fc00000u||(uintptr_t)allocation+bytes>0x4ffc0000u){kfree(allocation);return ERR_NOMEM;}
    memset(allocation,0,bytes);
    struct native_stream *s=allocation;s->allocation=allocation;
    uintptr_t p=((uintptr_t)(s+1)+DMA_ALIGN-1u)&~(uintptr_t)(DMA_ALIGN-1u);
    for(unsigned i=0;i<STREAM_NODES;i++)s->desc[i]=(void*)(p+i*DMA_ALIGN);
    s->payload=(void*)(p+STREAM_NODES*DMA_ALIGN);c->stream=s;
    const int sources[]={ETS_AXI_PDMA_IN_CH0_INTR_SOURCE,ETS_AXI_PDMA_IN_CH1_INTR_SOURCE,ETS_AXI_PDMA_IN_CH2_INTR_SOURCE};
    /* MeOS P4 vectors dispatch ESP-IDF allocated interrupts (also used by LCD).
     * Not IRAM-only: cache maintenance/callback follow the reference ISR ABI. */
    int error=esp_intr_alloc(sources[c->rx_channel],ESP_INTR_FLAG_LEVEL1|ESP_INTR_FLAG_INTRDISABLED,native_stream_isr,c,&s->irq);
    if(error!=ESP_OK){c->stream=NULL;kfree(allocation);return ERR_IO;}
    return 0;
}
static void stream_restore_tx_configuration(hal_parlio_t *c)
{
    uint32_t flags=arch_irq_disable();
    _parlio_ll_tx_set_clock_source(&PARL_IO,c->hz==500000?PARLIO_CLK_SRC_XTAL:PARLIO_CLK_SRC_PLL_F160M);
    hal_utils_clk_div_t div={.integer=(c->hz==500000?40000000u:160000000u)/c->hz};
    _parlio_ll_tx_set_clock_div(&PARL_IO,&div);
    arch_irq_restore(flags);
    parlio_ll_tx_set_bus_width(&PARL_IO,16);
    parlio_ll_tx_set_bit_pack_order(&PARL_IO,PARLIO_BIT_PACK_ORDER_LSB);
    parlio_ll_tx_treat_msb_as_valid(&PARL_IO,true);
    parlio_ll_tx_enable_clock_gating(&PARL_IO,true);
    PARL_IO.tx_clk_cfg.tx_clk_i_inv=1;PARL_IO.tx_clk_cfg.tx_clk_o_inv=1;
}
static int stream_first_start(hal_parlio_t *c,uint64_t deadline)
{
    struct native_stream *s=c->stream;
    c->stats.stage="stream_full_reset";
    /* Physical CLK remains GPIO HIGH; DIR/READY already HIGH. Reference does
     * APB -> source/divider -> running-clock Core/FIFO flush on first reply. */
    uint32_t flags=arch_irq_disable();
    _parlio_ll_rx_enable_clock(&PARL_IO,false);
    _parlio_ll_reset_register(0);
    _parlio_ll_rx_set_clock_source(&PARL_IO,PARLIO_CLK_SRC_PLL_F160M);
    hal_utils_clk_div_t div={.integer=160000000u/c->rx_hz};
    _parlio_ll_rx_set_clock_div(&PARL_IO,&div);
    arch_irq_restore(flags);
    stream_restore_tx_configuration(c);
    parlio_ll_rx_set_soft_recv_mode(&PARL_IO);
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);
    rcc_rx_clock(true);parlio_ll_rx_start(&PARL_IO,true);
    uint32_t settle=(16000000u+c->rx_hz-1u)/c->rx_hz;
    flags=arch_irq_disable();HP_SYS_CLKRST.hp_rst_en2.reg_rst_en_parlio_rx=1;arch_irq_restore(flags);
    PARL_IO.fifo_cfg.rx_fifo_srst=1;hal_delay_us(settle);
    flags=arch_irq_disable();HP_SYS_CLKRST.hp_rst_en2.reg_rst_en_parlio_rx=0;arch_irq_restore(flags);
    PARL_IO.fifo_cfg.rx_fifo_srst=0;
    parlio_ll_rx_set_bus_width(&PARL_IO,8);
    parlio_ll_rx_enable_clock_gating(&PARL_IO,false);
    parlio_ll_rx_set_soft_recv_mode(&PARL_IO);
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);
    PARL_IO.rx_clk_cfg.rx_clk_i_inv=0;PARL_IO.rx_clk_cfg.rx_clk_o_inv=0;
    parlio_ll_rx_set_bit_pack_order(&PARL_IO,PARLIO_BIT_PACK_ORDER_LSB);
    parlio_ll_rx_enable_timeout(&PARL_IO,false);
    parlio_ll_rx_start(&PARL_IO,true);hal_delay_us(settle);
    /* Normal stop ordering: clock OFF before START clear. */
    rcc_rx_clock(false);parlio_ll_rx_start(&PARL_IO,false);
    if(hal_time_now_us()>=deadline)return ERR_TIMEOUT;
    for(unsigned i=0;i<8;i++) {
        gpio_ll_output_disable(&GPIO,data_pins[i]);
        gpio_ll_set_output_enable_ctrl(&GPIO,data_pins[i],false,false);
        esp_rom_gpio_connect_in_signal(data_pins[i],parlio_periph_signals.groups[0].rx_units[0].data_sigs[i],false);
    }
    parlio_ll_rx_set_recv_bit_len(&PARL_IO,STREAM_RING*8u);
    parlio_ll_rx_set_eof_condition(&PARL_IO,PARLIO_LL_RX_EOF_COND_RX_FULL);
    parlio_ll_enable_interrupt(&PARL_IO,PARLIO_LL_EVENT_RX_MASK,false);
    parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_RX_MASK);
    for(unsigned i=0;i<STREAM_NODES;i++) {
        dma_descriptor_align8_t *d=s->desc[i];
        d->dw0.size=STREAM_BLOCK;d->dw0.length=STREAM_BLOCK;
        d->dw0.owner=DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        d->buffer=s->payload+i*STREAM_BLOCK;d->next=s->desc[(i+1u)%STREAM_NODES];
        if(sync_c2m(d,128))return ERR_IO;
    }
    if(sync_c2m(s->payload,STREAM_RING))return ERR_IO;
    axi_dma_ll_rx_enable_interrupt(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK,false);
    axi_dma_ll_rx_reset_channel(&AXI_DMA,c->rx_channel);
    axi_dma_ll_rx_abort(&AXI_DMA,c->rx_channel,false);
    axi_dma_ll_rx_clear_interrupt_status(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK);
    axi_dma_ll_rx_enable_owner_check(&AXI_DMA,c->rx_channel,false);
    axi_dma_ll_rx_enable_descriptor_burst(&AXI_DMA,c->rx_channel,true);
    axi_dma_ll_rx_set_burst_size(&AXI_DMA,c->rx_channel,16);
    axi_dma_ll_rx_enable_auto_return(&AXI_DMA,c->rx_channel,true);
    axi_dma_ll_rx_enable_etm_task(&AXI_DMA,c->rx_channel,false);
    axi_dma_ll_rx_connect_to_periph(&AXI_DMA,c->rx_channel,GDMA_TRIG_PERIPH_PARLIO,3);
    axi_dma_ll_rx_set_desc_addr(&AXI_DMA,c->rx_channel,(uint32_t)s->desc[0]);
    PARL_IO.reg_update.rx_reg_update=1;
    unsigned polls=0;
    while(PARL_IO.reg_update.rx_reg_update)if(hal_time_now_us()>=deadline||++polls>=POLL_LIMIT)return ERR_TIMEOUT;
    /* Reference prepare_clock(true): attach while the Core clock is OFF,
     * before soft validation/DMA/START/clock. A running-clock cold mux is a
     * different electrical start sequence, even if warm remapping matches. */
    esp_rom_gpio_connect_out_signal(CLOCK_PIN,parlio_periph_signals.groups[0].rx_units[0].clk_out_sig,true,false);
    gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
    gpio_ll_output_enable(&GPIO,CLOCK_PIN);
    /* Soft delimiter is armed before receive(), then DMA -> START -> clock. */
    parlio_ll_rx_start_soft_recv(&PARL_IO,true);
    axi_dma_ll_rx_enable_interrupt(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK,true);
    if(esp_intr_enable(s->irq)!=ESP_OK)return ERR_IO;
    axi_dma_ll_rx_start(&AXI_DMA,c->rx_channel);c->rx_started=true;
    parlio_ll_rx_start(&PARL_IO,true);rcc_rx_clock(true);s->started=true;
    return 0;
}
int hal_parlio_stream_begin(hal_parlio_t *c,hal_parlio_stream_block_t cb,void *arg,uint64_t deadline)
{
    if(!c||c!=native_owner||!cb||c->rx_hz<1000000)return ERR_INVAL;
    if(c->busy||c->failed||c->gate_armed)return ERR_BUSY;
    if(c->stream&&(c->stream->active||c->stream->fatal))return ERR_BUSY;
    if(!hal_gpio_read(CLOCK_PIN)||!hal_gpio_read(DIR_PIN)||!hal_gpio_read(READY_PIN))return ERR_BUSY;
    c->busy=true;int error=0;
    if(!c->stream)error=stream_allocate(c);
    if(!error) {
        struct native_stream *s=c->stream;
        if(s->started&&(s->callback!=cb||s->context!=arg))error=ERR_INVAL;
        else if(s->started&&!stream_phase_clock_ok(c,false))error=ERR_IO;
        else {
            s->callback=cb;s->context=arg;s->epoch_start=s->bytes;
            s->ready_latched=false;s->closed=false;s->stop_reason=0;
            s->last_callback_us=0;s->active=true;
            bool cold=!s->started;
            if(cold)error=stream_first_start(c,deadline);
            if(!error) {
                uint32_t flags=arch_irq_disable();
                if(!cold) {
                    parlio_ll_rx_start_soft_recv(&PARL_IO,true);
                    esp_rom_gpio_connect_out_signal(CLOCK_PIN,parlio_periph_signals.groups[0].rx_units[0].clk_out_sig,true,false);
                    gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
                    gpio_ll_output_enable(&GPIO,CLOCK_PIN);
                }
                arch_irq_restore(flags);
                c->stats.rx_phase_verified=PARL_IO.rx_clk_cfg.rx_clk_i_inv==0&&PARL_IO.rx_clk_cfg.rx_clk_o_inv==0&&GPIO.func_out_sel_cfg[CLOCK_PIN].out_inv_sel;
                if(!c->stats.rx_phase_verified||!stream_phase_clock_ok(c,true))error=ERR_IO;
            }
        }
    }
    if(error){c->failed=true;c->busy=false;hal_parlio_stream_stop(c);return error;}
    c->stats.stage="stream_active";c->busy=false;return 0;
}
void hal_parlio_stream_status(hal_parlio_t *c,hal_parlio_stream_status_t *out)
{
    if(!out)return;
    memset(out,0,sizeof *out);
    if(!c||c!=native_owner||!c->stream)return;
    uint32_t flags=arch_irq_disable();struct native_stream *s=c->stream;
    *out=(hal_parlio_stream_status_t){.blocks=s->blocks,.bytes=s->bytes,.epoch_bytes=s->bytes-s->epoch_start,
        .eofs=s->eofs,.stop_reason=s->stop_reason,.late_bytes=s->late_bytes,.irq_raw=s->irq_raw,
        .started=s->started,.closed=s->closed,.fatal=s->fatal,.ready_latched=s->ready_latched,
        .pending=axi_dma_ll_rx_get_interrupt_status(&AXI_DMA,c->rx_channel,true)!=0};
    arch_irq_restore(flags);
}
int hal_parlio_stream_pause(hal_parlio_t *c,uint64_t deadline)
{
    if(!c||c!=native_owner||!c->stream)return ERR_INVAL;
    if(c->busy)return ERR_BUSY;
    struct native_stream *s=c->stream;
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);s->active=false;
    bool phase_ok=stream_phase_clock_ok(c,true);
    stream_clock_gpio_low();__sync_synchronize();
    if(!phase_ok){s->fatal=true;c->failed=true;return ERR_IO;}
    unsigned polls=0;uint64_t quiet=hal_time_now_us();
    for(;;) {
        if(s->fatal||PARL_IO.int_raw.val&PARLIO_LL_EVENT_RX_FIFO_FULL){c->failed=true;return ERR_IO;}
        uint64_t now=hal_time_now_us();
        if(axi_dma_ll_rx_get_interrupt_status(&AXI_DMA,c->rx_channel,true))quiet=now;
        else if(now-quiet>=2){c->stats.stage="stream_paused";return 0;}
        if(now>=deadline||++polls>=POLL_LIMIT)return ERR_TIMEOUT;
        hal_delay_us(1); /* IRQ remains enabled so real pending callback drains. */
    }
}
bool hal_parlio_stream_stop(hal_parlio_t *c)
{
    if(!c||c!=native_owner||c->busy)return false;
    struct native_stream *s=c->stream;if(!s)return true;
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);s->active=false;
    axi_dma_ll_rx_enable_interrupt(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK,false);
    if(esp_intr_disable(s->irq)!=ESP_OK){c->failed=true;return false;}
    stream_clock_gpio_low();rcc_rx_clock(false);parlio_ll_rx_start(&PARL_IO,false);
    if(stop_dma(c,true)||c->dma_quarantined){c->failed=true;return false;}
    if(esp_intr_free(s->irq)!=ESP_OK){c->failed=true;return false;}
    void *allocation=s->allocation;c->stream=NULL;__sync_synchronize();kfree(allocation);
    restore_bus(false);return true;
}

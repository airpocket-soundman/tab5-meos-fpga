/* Finite, polling RX on the exclusively owned PARLIO module/AXI RX channel.
 * Included by parlio_native.c. No ISR, FreeRTOS object, DMA ring or GPIO fallback.
 * The caller must raise physical CLK while DIR=0, then set DIR=1 with CLK HIGH.
 * RX's native idle clock is LOW; matrix inversion preserves that physical HIGH.
 * Literal phase bits reproduce the tested GPU NEG0 path; SDK5.4's edge enum is
 * not assumed equivalent to the reference SDK5.5.2's names.
 */
static void rcc_rx_clock(bool enabled)
{
    uint32_t flags=arch_irq_disable();
    _parlio_ll_rx_enable_clock(&PARL_IO,enabled);
    arch_irq_restore(flags);
}

static uint32_t rx_reset_settle_us(uint32_t frequency_hz)
{
    if(frequency_hz!=500000&&frequency_hz!=1000000&&
       frequency_hz!=2000000&&frequency_hz!=5000000&&
       frequency_hz!=8000000&&frequency_hz!=10000000&&
       frequency_hz!=16000000&&frequency_hz!=20000000)return 0;
    /* At least two complete Core cycles, plus 2us for phase/CDC margin.
     * This is six microseconds at500kHz and four at1MHz; physical CLK remains
     * unmapped/HIGH throughout. The supported frequency set bounds this wait. */
    return (2000000u+frequency_hz-1u)/frequency_hz+2u;
}

static int native_rx(hal_parlio_t *c,uint8_t *dst,size_t n,uint64_t deadline,
                     hal_parlio_cancel_t cancel,void *arg)
{
    int error=check(deadline,cancel,arg);
    bool prepared=false,complete=false;
    uint32_t eof_desc=0;
    size_t capacity=(n+127u)&~(size_t)127u;
    uint32_t reset_settle_us=rx_reset_settle_us(c->rx_hz);
    c->stats.stage="rx_precondition";
    c->stats.last_rx_bytes=0;c->stats.rx_phase_verified=false;
    c->stats.rx_gate_verified=false;c->stats.rx_gate_closed=false;
    c->stats.gate_event_seen=false;c->stats.gate_task_seen=false;
    if(!dst||!n||n>HAL_PARLIO_MAX_RX||capacity>DMA_CAPACITY||!reset_settle_us)error=ERR_INVAL;
    if(!error&&(!hal_gpio_read(CLOCK_PIN)||!hal_gpio_read(DIR_PIN)||!hal_gpio_read(READY_PIN)))error=ERR_BUSY;
    if(!error&&hal_gpio_read(17)){c->stats.stage="rx_gate_not_low";error=ERR_BUSY;}

    if(!error) {
        /* Unmap every TX pad before any free-running/reset clocks. Selecting
         * GPIO input alone must not leave peripheral-controlled output enable. */
        for(unsigned i=0;i<8;i++) {
            hal_io_gpio_input(data_pins[i],0);
            gpio_ll_set_output_enable_ctrl(&GPIO,data_pins[i],false,false);
            esp_rom_gpio_connect_in_signal(data_pins[i],
                parlio_periph_signals.groups[0].rx_units[0].data_sigs[i],false);
        }
        /* GPIO17 is separately owned/configured LOW by the IO caller. It is
         * only an internal VALID source, not a ninth data bit or wire. */
        gpio_ll_input_enable(&GPIO,17);
        esp_rom_gpio_connect_in_signal(17,
            parlio_periph_signals.groups[0].rx_units[0].data_sigs[15],false);
        gpio_ll_set_level(&GPIO,CLOCK_PIN,1);
        esp_rom_gpio_connect_out_signal(CLOCK_PIN,SIG_GPIO_OUT_IDX,false,false);
        gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
        gpio_ll_output_enable(&GPIO,CLOCK_PIN);
        if(!hal_gpio_read(CLOCK_PIN)){c->stats.stage="rx_clock_handoff";error=ERR_IO;}
    }
    if(!error) {
        c->stats.stage="rx_free_running_reset";
        /* Startup APB reset is done in open before GDMA/Core. Per frame only
         * reset this owned channel, RX Core and async FIFO, not shared APB. */
        axi_dma_ll_rx_enable_interrupt(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK,false);
        axi_dma_ll_rx_clear_interrupt_status(&AXI_DMA,c->rx_channel,GDMA_LL_RX_EVENT_MASK);
        axi_dma_ll_rx_reset_channel(&AXI_DMA,c->rx_channel);
        axi_dma_ll_rx_abort(&AXI_DMA,c->rx_channel,false);
        uint32_t flags=arch_irq_disable();
        _parlio_ll_rx_set_clock_source(&PARL_IO,
            c->rx_hz==500000?PARLIO_CLK_SRC_XTAL:PARLIO_CLK_SRC_PLL_F160M);
        hal_utils_clk_div_t div={.integer=(c->rx_hz==500000?40000000u:160000000u)/c->rx_hz,
                                 .numerator=0,.denominator=0};
        _parlio_ll_rx_set_clock_div(&PARL_IO,&div);
        _parlio_ll_rx_enable_clock(&PARL_IO,true);
        _parlio_ll_rx_reset_clock(&PARL_IO);
        arch_irq_restore(flags);
        hal_delay_us(reset_settle_us);
        error=check(deadline,cancel,arg);
        if(!error) {
            parlio_ll_rx_reset_fifo(&PARL_IO);
            hal_delay_us(reset_settle_us);
            error=check(deadline,cancel,arg);
        }
        rcc_rx_clock(false);
        parlio_ll_rx_start(&PARL_IO,false);
        parlio_ll_rx_start_soft_recv(&PARL_IO,false);
        if(!error)error=check(deadline,cancel,arg);
    }
    if(!error) {
        c->stats.stage="rx_configure";
        parlio_ll_rx_set_bus_width(&PARL_IO,8);
        parlio_ll_rx_set_bit_pack_order(&PARL_IO,PARLIO_BIT_PACK_ORDER_LSB);
        parlio_ll_rx_set_level_recv_mode(&PARL_IO,false);
        parlio_ll_rx_treat_data_line_as_en(&PARL_IO,15);
        parlio_ll_rx_set_eof_condition(&PARL_IO,PARLIO_LL_RX_EOF_COND_RX_FULL);
        parlio_ll_rx_set_recv_bit_len(&PARL_IO,(uint32_t)n*8u);
        parlio_ll_rx_enable_clock_gating(&PARL_IO,true);
        parlio_ll_rx_enable_timeout(&PARL_IO,false);
        PARL_IO.rx_clk_cfg.rx_clk_i_inv=0;PARL_IO.rx_clk_cfg.rx_clk_o_inv=0;
        parlio_ll_enable_interrupt(&PARL_IO,PARLIO_LL_EVENT_RX_MASK,false);
        parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_RX_MASK);
        memset(c->payload,0,capacity);memset(c->desc,0,128);
        c->desc->dw0.size=capacity;c->desc->dw0.length=0;
        c->desc->dw0.owner=DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        c->desc->buffer=c->payload;c->desc->next=NULL;
        c->stats.stage="rx_payload_flush";
        error=sync_c2m(c->payload,capacity);
        if(!error){c->stats.stage="rx_descriptor_flush";error=sync_c2m(c->desc,128);}
        if(!error) {
            axi_dma_ll_rx_enable_owner_check(&AXI_DMA,c->rx_channel,true);
            axi_dma_ll_rx_enable_descriptor_burst(&AXI_DMA,c->rx_channel,true);
            axi_dma_ll_rx_set_burst_size(&AXI_DMA,c->rx_channel,16);
            axi_dma_ll_rx_enable_auto_return(&AXI_DMA,c->rx_channel,false);
            axi_dma_ll_rx_enable_etm_task(&AXI_DMA,c->rx_channel,false);
            axi_dma_ll_rx_connect_to_periph(&AXI_DMA,c->rx_channel,GDMA_TRIG_PERIPH_PARLIO,3);
            axi_dma_ll_rx_set_desc_addr(&AXI_DMA,c->rx_channel,(uint32_t)c->desc);
            prepared=true;
            /* Do not use the LL helper's unbounded REG_UPDATE busy wait. */
            PARL_IO.reg_update.rx_reg_update=1;
            c->stats.stage="rx_register_update";
            unsigned polls=0;
            while(PARL_IO.reg_update.rx_reg_update) {
                error=check(deadline,cancel,arg);
                if(error||++polls>=POLL_LIMIT){if(!error)error=ERR_TIMEOUT;break;}
            }
        }
    }
    if(!error) {
        c->stats.stage="rx_gate_arm";
        error=rx_gate_arm(c);
        if(!error&&!rx_gate_ready(c)){c->stats.stage="rx_gate_unverified";error=ERR_IO;}
    }
    if(!error) {
        c->stats.stage="rx_start";
        axi_dma_ll_rx_start(&AXI_DMA,c->rx_channel);c->rx_started=true;
        /* Official clock OFF -> START=1 -> clock ON order. The pad handoff
         * while the gated RX clock is idle HIGH cannot add a leading strobe. */
        parlio_ll_rx_start(&PARL_IO,true);
        esp_rom_gpio_connect_out_signal(CLOCK_PIN,
            parlio_periph_signals.groups[0].rx_units[0].clk_out_sig,true,false);
        gpio_ll_set_output_enable_ctrl(&GPIO,CLOCK_PIN,false,false);
        c->stats.rx_phase_verified=PARL_IO.rx_clk_cfg.rx_clk_i_inv==0&&
            PARL_IO.rx_clk_cfg.rx_clk_o_inv==0&&GPIO.func_out_sel_cfg[CLOCK_PIN].out_inv_sel;
        if(!c->stats.rx_phase_verified){c->stats.stage="rx_phase_attach";error=ERR_IO;}
        else {
            /* VALID remains LOW while clocks/start/route become ready. EOF's
             * owned ETM task clears GPIO17 without waiting for the polling
             * task or an ISR, closing sampling/output clock at finite EOF. */
            rcc_rx_clock(true);
            hal_gpio_write(17,1);
        }
    }
    if(!error) {
        c->stats.stage="rx_eof";
        unsigned polls=0;
        for(;;) {
            c->stats.parlio_raw=PARL_IO.int_raw.val;
            c->stats.dma_raw=axi_dma_ll_rx_get_interrupt_status(&AXI_DMA,c->rx_channel,true);
            /* RX error bits: errEOF, descriptor error/empty, FIFO L1/L2/L3 errors. */
            if(c->stats.parlio_raw&PARLIO_LL_EVENT_RX_FIFO_FULL){c->stats.stage="rx_fifo_overflow";error=ERR_IO;break;}
            if(c->stats.dma_raw&0x7fcu){c->stats.stage="rx_dma_error";error=ERR_IO;break;}
            if(c->stats.dma_raw&(1u<<1)) {
                eof_desc=axi_dma_ll_rx_get_success_eof_desc_addr(&AXI_DMA,c->rx_channel);
                complete=true;break;
            }
            error=check(deadline,cancel,arg);
            if(error||++polls>=POLL_LIMIT){
                if(!error)error=ERR_TIMEOUT;
                c->stats.stage=error==HAL_PARLIO_CANCELLED?"rx_eof_cancelled":"rx_eof_timeout";
                break;
            }
        }
    }
    if(!error&&complete) {
        c->stats.stage="rx_eof_gate";
        uint64_t gate_started=hal_time_now_us();
        unsigned polls=0;
        while(!rx_gate_closed(c)) {
            error=check(deadline,cancel,arg);
            if(error){c->stats.stage=error==HAL_PARLIO_CANCELLED?"rx_eof_gate_cancelled":"rx_eof_gate_timeout";break;}
            if(hal_time_now_us()-gate_started>=reset_settle_us||++polls>=POLL_LIMIT) {
                c->stats.stage="rx_eof_gate_unproven";error=ERR_IO;break;
            }
        }
        if(!error) {
            /* Keep overflow detection strict, including a status bit that
             * arrived between the initial EOF poll and the gate observation. */
            c->stats.parlio_raw=PARL_IO.int_raw.val;
            c->stats.dma_raw=axi_dma_ll_rx_get_interrupt_status(&AXI_DMA,c->rx_channel,true);
            if(c->stats.parlio_raw&PARLIO_LL_EVENT_RX_FIFO_FULL){c->stats.stage="rx_fifo_overflow";error=ERR_IO;}
            else if(c->stats.dma_raw&0x7fcu){c->stats.stage="rx_dma_error";error=ERR_IO;}
        }
    }
    /* Always stop the actual sample clock before clearing RX_START. Never
     * inspect/reuse DMA storage until the owner's stop helper proves quiescence. */
    rcc_rx_clock(false);parlio_ll_rx_start(&PARL_IO,false);
    parlio_ll_rx_start_soft_recv(&PARL_IO,false);
    rx_gate_disarm(c);
    int stopped=stop_dma(c,true);
    if(stopped&&!error)error=stopped;
    restore_bus(true);
    parlio_ll_clear_interrupt_status(&PARL_IO,PARLIO_LL_EVENT_RX_MASK);
    if(prepared&&!stopped) {
        int synced=sync_m2c(c->desc,128);
        if(synced&&!error){c->stats.stage="rx_descriptor_cache";error=synced;}
        if(!synced) {
            c->stats.last_rx_bytes=c->desc->dw0.length;
            if(!error) {
                if(!complete)c->stats.stage="rx_missing_eof";
                else if(eof_desc!=(uint32_t)c->desc)c->stats.stage="rx_eof_address";
                else if(c->desc->dw0.owner)c->stats.stage="rx_descriptor_owner";
                else if(!c->desc->dw0.suc_eof)c->stats.stage="rx_descriptor_eof";
                else if(c->desc->dw0.err_eof)c->stats.stage="rx_descriptor_error";
                else if(c->desc->dw0.length!=n)c->stats.stage="rx_descriptor_length";
                else c->stats.stage="rx_descriptor_valid";
                if(!complete||eof_desc!=(uint32_t)c->desc||c->desc->dw0.owner||
                   !c->desc->dw0.suc_eof||c->desc->dw0.err_eof||c->desc->dw0.length!=n)error=ERR_IO;
            }
        }
        if(!synced)synced=sync_m2c(c->payload,capacity);
        if(synced&&!error){c->stats.stage="rx_payload_cache";error=synced;}
    }
    if(!error) {
        error=check(deadline,cancel,arg);
        if(error)c->stats.stage=error==HAL_PARLIO_CANCELLED?"rx_cancelled_after_stop":"rx_deadline_after_stop";
    }
    if(!error&&(!c->stats.rx_phase_verified||PARL_IO.rx_clk_cfg.rx_clk_i_inv||PARL_IO.rx_clk_cfg.rx_clk_o_inv)){
        c->stats.stage="rx_phase_completion";error=ERR_IO;
    }
    if(!error){memcpy(dst,c->payload,n);c->stats.stage="rx_complete";}
    return error;
}

/*
 * M5EX link layout.  Everything is linked at address 0; the loader adds the real
 * base to every word listed in the relocation table.
 *
 *   [ code + literals ][ rodata ][ data ]  = the image stored in the file
 *   [ bss ]                                = zeroed by the loader
 *
 * __m5ex_code_end separates what executes (reached through the I-bus alias)
 * from what is only data.
 */
ENTRY(app_main)
SECTIONS
{
    . = 0;
    .text : ALIGN(4) {
        *(.literal .literal.* .text .text.*)
        . = ALIGN(4);
        __m5ex_code_end = .;
    }
    .rodata : ALIGN(4) { KEEP(*(.rodata.m5ex_manifest)) *(.rodata .rodata.* .rodata1) . = ALIGN(4); }
    .data   : ALIGN(4) { *(.data .data.* .sdata .sdata.*) . = ALIGN(4); __m5ex_image_end = .; }
    .bss    : ALIGN(4) { *(.bss .bss.* COMMON) . = ALIGN(4); __m5ex_mem_end = .; }
    /DISCARD/ : { *(.comment) *(.xt.*) *(.xtensa.info) *(.note*) *(.eh_frame*) *(.debug*) *(.xt.prop) *(.xt.lit) }
}

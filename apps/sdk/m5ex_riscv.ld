/* ESP32-P4 M5EX: PC-relative text, explicit relocatable data words.
 * No global pointer, dynamic linker, GOT, PLT or kernel-symbol resolution. */
OUTPUT_ARCH(riscv)
ENTRY(app_main)
SECTIONS
{
    . = 0;
    .text : ALIGN(4) {
        *(.text.app_main)
        *(.text .text.*)
        . = ALIGN(4);
        __m5ex_code_end = .;
    }
    .rodata : ALIGN(4) { KEEP(*(.rodata.m5ex_manifest)) *(.rodata .rodata.* .srodata .srodata.*) . = ALIGN(4); }
    .data : ALIGN(4) { *(.data .data.* .sdata .sdata.*) . = ALIGN(4); __m5ex_image_end = .; }
    .bss : ALIGN(4) { *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __m5ex_mem_end = .; }
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) *(.debug*) }
}

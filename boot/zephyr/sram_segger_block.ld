/* SEGGER SRAM */
.sram_segger_block 0x20000000 (NOLOAD) :  ALIGN_WITH_INPUT
{
    . = ALIGN(4);
    __ssb_start = .;
    *(.sram_segger)
    __ssb_end = ALIGN(4);
} > SRAM

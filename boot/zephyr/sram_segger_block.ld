SECTION_PROLOGUE(.sram_segger,0x20000000 (NOLOAD),)
{
    _sram_segger_start = .;
    KEEP(*(.sram_segger))
    _sram_segger_end = .;
} GROUP_LINK_IN(RAMABLE_REGION)
_sram_segger_size = _sram_segger_end - _sram_segger_start;

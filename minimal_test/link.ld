/* Minimal linker script for STM32N6 SRAM execution */

MEMORY
{
    /* SRAM2 - use base address for simpler loading */
    sram (rwx) : ORIGIN = 0x24100000, LENGTH = 256K
}

ENTRY(reset_handler)

SECTIONS
{
    .text :
    {
        *(.vectors)
        *(.text*)
        *(.rodata*)
    } > sram

    .data :
    {
        *(.data*)
    } > sram

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } > sram

    /DISCARD/ :
    {
        *(.ARM.*)
        *(.comment)
        *(.debug*)
    }
}

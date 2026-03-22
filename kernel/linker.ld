ENTRY(_start)

SECTIONS {
    . = 0x3000;

    .text : {
        *(.text.boot)
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}

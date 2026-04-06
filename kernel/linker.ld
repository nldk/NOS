ENTRY(_start)

SECTIONS {
    . = 0x3000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame*)
        *(.eh_frame_hdr*)
        *(.comment*)
    }
}

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC;
}
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.header)) *(.text .text.*) *(.plt) } :text
    . = ALIGN(0x1000);
    HIDDEN(__ro_start = .);
    .rodata : { *(.rodata .rodata.*) } :rodata
    .dynsym : { *(.dynsym) } :rodata
    .dynstr : { *(.dynstr) } :rodata
    .hash : { *(.hash) } :rodata
    .rel.dyn : { *(.rel.dyn) } :rodata
    .rel.plt : { *(.rel.plt) } :rodata
    . = ALIGN(0x1000);
    HIDDEN(__data_start = .);
    .data : { *(.data .data.*) } :data
    .got : { *(.got .got.plt) } :data
    .dynamic : { *(.dynamic) } :data :dynamic
    . = ALIGN(0x1000);
    HIDDEN(__data_end = .);
    HIDDEN(__bss_start = .);
    .bss (NOLOAD) : {
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(8);
        HIDDEN(__module_object = .);
        . += 0x100;
        . = ALIGN(0x1000);
    } :data
    HIDDEN(__bss_end = .);
    HIDDEN(__ro_size = ABSOLUTE(__data_start - __ro_start));
    HIDDEN(__data_size = ABSOLUTE(__data_end - __data_start));
    HIDDEN(__bss_size = ABSOLUTE(__bss_end - __bss_start));
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.interp) }
}

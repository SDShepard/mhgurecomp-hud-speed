    .section .text.header, "ax"
    .arm
    .global _start
_start:
    b .
    .word __mod0 - _start
    .space 8
    .ascii "NRO0"
    .word 0
    .word __data_end - _start
    .word 0
    .word 0, __ro_start - _start
    .word __ro_start - _start, __ro_size
    .word __data_start - _start, __data_size
    .word __bss_size
    .word 0
    .space 0x80 - (. - _start)
__mod0:
    .ascii "MOD0"
    .word _DYNAMIC - __mod0
    .word __bss_start - __mod0
    .word __bss_end - __mod0
    .word 0, 0, __module_object - __mod0

.syntax unified
.cpu cortex-m3
.thumb

.section .isr_vector, "a"
.word 0x20005000          @ top of RAM
.word reset_handler
.word default_handler     @ NMI
.word default_handler     @ HardFault
.word default_handler     @ MemManage
.word default_handler     @ BusFault
.word default_handler     @ UsageFault
.word default_handler     @ Reserved
.word default_handler     @ Reserved
.word default_handler     @ Reserved
.word default_handler     @ Reserved
.word default_handler     @ SVCall
.word default_handler     @ Debug Monitor
.word default_handler     @ Reserved
.word default_handler     @ PendSV

.section .text
.thumb_func
.global reset_handler
reset_handler:
    @ copy .data from flash to RAM
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
copy_data:
    cmp r0, r1
    bge zero_bss
    ldr r3, [r2], #4
    str r3, [r0], #4
    b copy_data

zero_bss:
    @ zero out .bss
    ldr r0, =_sbss
    ldr r1, =_ebss
    mov r2, #0
zero_loop:
    cmp r0, r1
    bge call_main
    str r2, [r0], #4
    b zero_loop

call_main:
    bl main
    b .

.thumb_func
default_handler:
    b .

/* TC 0.0.46 ARM32 Cortex-M7 entry point for the MPS2 AN500 platform.
 *
 * M-profile boots from the vector table at address 0: the first word is the
 * initial stack pointer and the second is the reset handler.  The code and
 * static regions live above the vector page as described by link.ld; address
 * 0 itself is never a language object.  FPv5-D is enabled before generated
 * code or the runtime performs any binary64 operation, and unaligned scalar
 * accesses are left architecturally enabled (CCR.UNALIGN_TRP = 0).
 */
    .syntax unified
    .cpu cortex-m7
    .fpu fpv5-d16
    .thumb

    .section .vectors,"ax"
    .p2align 2
    .globl __tc_vector_table
__tc_vector_table:
    .word __tc_stack_top
    .word _start

    .text
    .p2align 2
    .globl _start
    .thumb_func
    .type _start, %function
_start:
    ldr r0, =0xE000ED88          /* CPACR */
    ldr r1, [r0]
    orr r1, r1, #(0xF << 20)     /* CP10/CP11 full access */
    str r1, [r0]
    dsb
    isb
    ldr r0, =0xE000ED14          /* CCR */
    ldr r1, [r0]
    bic r1, r1, #(1 << 3)        /* allow unaligned scalar access */
    str r1, [r0]
    ldr r0, =__tc_stack_top
    mov sp, r0
    bl tc_main
    ldr r0, =__tc_rt_normal_status
    ldr r0, [r0]
    bl tc_rt_runtime_exit
1:
    b 1b
    .size _start, .-_start
    .section .note.GNU-stack,"",%progbits

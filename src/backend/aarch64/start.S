/* TC 0.0.46 AArch64 subset entry point.
 *
 * QEMU virt RAM starts at 0x40000000; the TC code is linked there.  The stack
 * grows down from the platform stack top supplied by the linker script.
 * tc_main is emitted by the subset backend; a normal return follows the
 * profile's normal exit.  This image uses semihosting for its platform I/O and
 * is executed by qemu-system-aarch64; an EL0/Linux-user runtime is a separate
 * platform instance.
 */
    .text
    .p2align 2
    .globl _start
    .type _start, %function
_start:
    /* The subset image uses only general-purpose registers and aligned
     * accesses (the runtime is built with -mgeneral-regs-only and
     * -mstrict-align), so it does not need privileged FP/SIMD or alignment
     * setup.  The stack and input addresses are fixed by the linker script. */
    ldr x0, =__tc_stack_top
    mov sp, x0
    bl tc_main
    mov w0, #0
    bl tc_aarch64_runtime_exit
1:
    b 1b
    .size _start, .-_start
    .section .note.GNU-stack,"",%progbits

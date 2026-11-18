/* TC 0.0.46 RISC-V 32 entry point for the QEMU virt platform instance.
 *
 * QEMU loads the ELF at RAM base 0x80000000 and starts at _start.  The stack
 * grows down from __tc_stack_top defined by the linker script.  tc_main is
 * emitted by the backend; a normal return follows the profile's normal exit.
 * The runtime uses semihosting only for the exit record channel; program
 * stdout/stdin are the NS16550A UART.
 */
    .section .text._start,"ax",@progbits
    .p2align 2
    .globl _start
    .type _start, @function
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    /* Enable the F/D floating-point state in mstatus (FS = Initial).  Until
     * this is set, every F/D instruction raises an illegal-instruction
     * exception even when the hart implements the extension. */
    csrr t0, mstatus
    li t1, 0x6000
    or t0, t0, t1
    csrw mstatus, t0
    csrwi fcsr, 0
    la sp, __tc_stack_top
    call tc_main
    lui t0, %hi(__tc_rt_normal_status)
    lw a0, %lo(__tc_rt_normal_status)(t0)
    call tc_rt_runtime_exit
1:
    j 1b
    .size _start, .-_start
    .section .note.GNU-stack,"",@progbits

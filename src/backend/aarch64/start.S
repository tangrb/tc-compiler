/* TC 0.0.46 AArch64 entry point.
 *
 * QEMU virt RAM starts at 0x40000000; the TC code is linked there.  The stack
 * grows down from the platform stack top supplied by the linker script.
 * tc_main is emitted by the backend; a normal return follows the profile's
 * normal exit.  This image uses semihosting for its platform I/O and is
 * executed by qemu-system-aarch64; an EL0/Linux-user runtime is a separate
 * platform instance.
 *
 * Floating-point helpers need CPACR_EL1.FPEN, and the backend relies on
 * unaligned scalar accesses being architecturally permitted (standard §6.7),
 * so SCTLR_EL1.A is cleared before any generated code runs.
 */
    .text
    .p2align 2
    .globl _start
    .type _start, %function
_start:
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20)
    msr cpacr_el1, x0
    isb
    mrs x0, sctlr_el1
    bic x0, x0, #(1 << 1)
    msr sctlr_el1, x0
    isb
    ldr x0, =__tc_stack_top
    mov sp, x0
    bl tc_main
    ldr x0, =__tc_a64_normal_status
    ldr x0, [x0]
    bl tc_a64_runtime_exit
1:
    b 1b
    .size _start, .-_start
    .section .note.GNU-stack,"",%progbits

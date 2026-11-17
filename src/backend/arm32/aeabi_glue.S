/* TC 0.0.46 ARM32 ARM EABI helper glue.
 *
 * `__aeabi_uldivmod` returns the quotient in r0:r1 and the remainder in
 * r2:r3.  The C helpers return the quotient in r0:r1 and write the remainder
 * through a stack pointer, so these wrappers only marshal registers.  The
 * memory helpers tail-call the runtime's all-or-nothing implementations.
 */
    .syntax unified
    .cpu cortex-m7
    .fpu fpv5-d16
    .thumb
    .text

    .p2align 2
    .globl __aeabi_uldivmod
    .thumb_func
    .type __aeabi_uldivmod, %function
__aeabi_uldivmod:
    push {r4, lr}
    sub sp, sp, #24
    add r4, sp, #8
    str r4, [sp]
    bl tc_arm32_uldivmod64
    ldr r2, [r4]
    ldr r3, [r4, #4]
    add sp, sp, #24
    pop {r4, pc}
    .size __aeabi_uldivmod, .-__aeabi_uldivmod

    .p2align 2
    .globl __aeabi_ldivmod
    .thumb_func
    .type __aeabi_ldivmod, %function
__aeabi_ldivmod:
    push {r4, lr}
    sub sp, sp, #24
    add r4, sp, #8
    str r4, [sp]
    bl tc_arm32_ldivmod64
    ldr r2, [r4]
    ldr r3, [r4, #4]
    add sp, sp, #24
    pop {r4, pc}
    .size __aeabi_ldivmod, .-__aeabi_ldivmod

    .p2align 2
    .globl __aeabi_memcpy
    .thumb_func
    .type __aeabi_memcpy, %function
__aeabi_memcpy:
    b memcpy
    .size __aeabi_memcpy, .-__aeabi_memcpy

    .p2align 2
    .globl __aeabi_memcpy4
    .thumb_func
    .type __aeabi_memcpy4, %function
__aeabi_memcpy4:
    b memcpy
    .size __aeabi_memcpy4, .-__aeabi_memcpy4

    .p2align 2
    .globl __aeabi_memcpy8
    .thumb_func
    .type __aeabi_memcpy8, %function
__aeabi_memcpy8:
    b memcpy
    .size __aeabi_memcpy8, .-__aeabi_memcpy8

    .p2align 2
    .globl __aeabi_memmove
    .thumb_func
    .type __aeabi_memmove, %function
__aeabi_memmove:
    b memmove
    .size __aeabi_memmove, .-__aeabi_memmove

    .p2align 2
    .globl __aeabi_memmove4
    .thumb_func
    .type __aeabi_memmove4, %function
__aeabi_memmove4:
    b memmove
    .size __aeabi_memmove4, .-__aeabi_memmove4

    .p2align 2
    .globl __aeabi_memset
    .thumb_func
    .type __aeabi_memset, %function
__aeabi_memset:
    /* AAPCS: (dst r0, n r1, c r2) -> memset(dst, c, n). */
    mov r12, r1
    mov r1, r2
    mov r2, r12
    b memset
    .size __aeabi_memset, .-__aeabi_memset

    .p2align 2
    .globl __aeabi_memset4
    .thumb_func
    .type __aeabi_memset4, %function
__aeabi_memset4:
    mov r12, r1
    mov r1, r2
    mov r2, r12
    b memset
    .size __aeabi_memset4, .-__aeabi_memset4

    .p2align 2
    .globl __aeabi_memclr
    .thumb_func
    .type __aeabi_memclr, %function
__aeabi_memclr:
    mov r2, r1
    movs r1, #0
    b memset
    .size __aeabi_memclr, .-__aeabi_memclr

    .p2align 2
    .globl __aeabi_memclr4
    .thumb_func
    .type __aeabi_memclr4, %function
__aeabi_memclr4:
    mov r2, r1
    movs r1, #0
    b memset
    .size __aeabi_memclr4, .-__aeabi_memclr4

    .section .note.GNU-stack,"",%progbits

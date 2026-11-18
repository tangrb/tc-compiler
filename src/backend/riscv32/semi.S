/* TC 0.0.46 RISC-V 32 semihosting record channel.
 *
 * QEMU recognizes the RISC-V semihosting call by the three uncompressed
 * instructions below, which must not cross a page boundary.  The entry point
 * is aligned and the magic sequence starts on a 16-byte boundary.  a0 carries
 * SYS_WRITE0 and a1 the NUL-terminated string; the semihosting result is
 * returned in a0.
 */
    .text
    .p2align 4
    .globl tc_rv32_semihost_write0
    .type tc_rv32_semihost_write0, @function
tc_rv32_semihost_write0:
    mv a1, a0
    li a0, 4
    .p2align 4
    .option push
    .option norvc
    slli zero, zero, 0x1f
    ebreak
    srai zero, zero, 0x7
    .option pop
    ret
    .size tc_rv32_semihost_write0, .-tc_rv32_semihost_write0
    .section .note.GNU-stack,"",@progbits

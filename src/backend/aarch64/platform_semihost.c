/* TC 0.0.46 AArch64 hosted/semihosting platform seams (design §7.2/§8).
 *
 * Program stdout uses the QEMU semihosting console so it appears on the
 * harness stdout stream.  The exit record uses the PL011 UART so a trap code
 * is observed without contaminating program output; termination uses the
 * semihosting exit extended reason block.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_platform.h"

#define TC_PL011_BASE UINT64_C(0x09000000)

#define TC_SEMIHOST_SYS_WRITE0 0x04
#define TC_SEMIHOST_SYS_EXIT 0x18
#define TC_SEMIHOST_SYS_EXIT_EXTENDED 0x20
#define TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT 0x20000u

static long semihosting_call(unsigned long op, unsigned long arg) {
    register unsigned long x0 __asm__("x0") = op;
    register unsigned long x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (long)x0;
}

static void semihosting_write0(const char *text) {
    (void)semihosting_call(TC_SEMIHOST_SYS_WRITE0,
                           (unsigned long)(uintptr_t)text);
}

static void uart_init(void) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)TC_PL011_BASE;
    base[0x30u / 4u] = 0x301u; /* UARTEN | TXE | RXE */
    base[0x44u / 4u] = 0x7ffu; /* clear all interrupts */
}

static void uart_putc(char c) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)TC_PL011_BASE;
    while ((base[0x18u / 4u] & (1u << 5)) != 0) {
        /* wait while the TX FIFO is full */
    }
    base[0] = (uint32_t)(unsigned char)c;
}

void tc_rt_platform_stdout(const char *bytes, size_t size) {
    char chunk[128];
    size_t offset = 0;
    while (offset < size) {
        size_t take = size - offset;
        size_t i;
        if (take > sizeof(chunk) - 1u) {
            take = sizeof(chunk) - 1u;
        }
        for (i = 0; i < take; i++) {
            chunk[i] = bytes[offset + i];
        }
        chunk[take] = '\0';
        semihosting_write0(chunk);
        offset += take;
    }
}

void tc_rt_platform_record(const char *bytes, size_t size) {
    size_t i;
    uart_init();
    for (i = 0; i < size; i++) {
        uart_putc(bytes[i]);
    }
}

__attribute__((noreturn)) void tc_rt_platform_exit(uint32_t status) {
    struct {
        uint32_t reason;
        uint32_t exit_code;
    } block;
    block.reason = TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT;
    block.exit_code = status;
    (void)semihosting_call(TC_SEMIHOST_SYS_EXIT_EXTENDED,
                           (unsigned long)(uintptr_t)&block);
    (void)semihosting_call(TC_SEMIHOST_SYS_EXIT,
                           (unsigned long)(TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT +
                                           status));
    for (;;) {
    }
}

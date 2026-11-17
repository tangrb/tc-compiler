/* TC 0.0.46 AArch64 bare-metal platform seams (design §7.2/§8).
 *
 * Program stdout is PL011 UART0.  The exit record uses PL011 UART1 so the two
 * streams stay separate; the image terminates through PSCI SYSTEM_OFF on the
 * QEMU virt machine.  No semihosting calls are made, so the image runs with
 * `-semihosting-config` absent entirely.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_platform.h"

#define TC_PL011_STDOUT_BASE UINT64_C(0x09000000)
#define TC_PL011_RECORD_BASE UINT64_C(0x09040000)
#define TC_PSCI_SYSTEM_OFF UINT64_C(0x84000008)

static void uart_init_at(uint64_t base) {
    volatile uint32_t *registers = (volatile uint32_t *)(uintptr_t)base;
    registers[0x30u / 4u] = 0x301u; /* UARTEN | TXE | RXE */
    registers[0x44u / 4u] = 0x7ffu; /* clear all interrupts */
}

static void uart_putc_at(uint64_t base, char c) {
    volatile uint32_t *registers = (volatile uint32_t *)(uintptr_t)base;
    while ((registers[0x18u / 4u] & (1u << 5)) != 0) {
        /* wait while the TX FIFO is full */
    }
    registers[0] = (uint32_t)(unsigned char)c;
}

void tc_rt_platform_stdout(const char *bytes, size_t size) {
    size_t i;
    static int initialized = 0;
    if (!initialized) {
        uart_init_at(TC_PL011_STDOUT_BASE);
        initialized = 1;
    }
    for (i = 0; i < size; i++) {
        uart_putc_at(TC_PL011_STDOUT_BASE, bytes[i]);
    }
}

void tc_rt_platform_record(const char *bytes, size_t size) {
    size_t i;
    static int initialized = 0;
    if (!initialized) {
        uart_init_at(TC_PL011_RECORD_BASE);
        initialized = 1;
    }
    for (i = 0; i < size; i++) {
        uart_putc_at(TC_PL011_RECORD_BASE, bytes[i]);
    }
}

__attribute__((noreturn)) void tc_rt_platform_exit(uint32_t status) {
    register unsigned long x0 __asm__("x0") = (unsigned long)TC_PSCI_SYSTEM_OFF;
    (void)status;
    __asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
    for (;;) {
    }
}

/* TC 0.0.46 ARM32 MPS2 platform seams (design §7.2/§8).
 *
 * The target is the Cortex-M7 MPS2 AN500 image: program stdout uses the QEMU
 * semihosting console, the exit record uses CMSDK APB UART0 so trap codes do
 * not mix with program output, and termination uses the semihosting exit
 * extended block.  The runtime is freestanding; semihosting is an execution
 * environment service, not a C library dependency.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_platform.h"

#define TC_CMSDK_UART0_BASE UINT32_C(0x40004000)

#define TC_SEMIHOST_SYS_WRITE0 0x04
#define TC_SEMIHOST_SYS_EXIT_EXTENDED 0x20
#define TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT 0x20000u

static long semihosting_call(unsigned long op, unsigned long arg) {
    register unsigned long r0 __asm__("r0") = op;
    register unsigned long r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return (long)r0;
}

static void semihosting_write0(const char *text) {
    (void)semihosting_call(TC_SEMIHOST_SYS_WRITE0,
                           (unsigned long)(uintptr_t)text);
}

static void uart_init(void) {
    volatile uint32_t *base =
        (volatile uint32_t *)(uintptr_t)TC_CMSDK_UART0_BASE;
    base[0x8u / 4u] = 0x3u; /* TXEN | RXEN */
}

static void uart_putc(char c) {
    volatile uint32_t *base =
        (volatile uint32_t *)(uintptr_t)TC_CMSDK_UART0_BASE;
    while ((base[0x4u / 4u] & 1u) != 0) {
        /* wait while the transmit buffer is full */
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
    static int initialized = 0;
    if (!initialized) {
        uart_init();
        initialized = 1;
    }
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
    for (;;) {
    }
}

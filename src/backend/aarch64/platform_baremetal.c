/* TC 0.0.46 AArch64 bare-metal platform seams (design §7.2/§8).
 *
 * Program stdout and stdin are PL011 UART0 (a bidirectional chardev), and the
 * exit record uses PL011 UART1 so control records do not share the program
 * stream.  The image terminates through PSCI SYSTEM_OFF on the QEMU
 * virt machine.  No semihosting calls are made, so the image runs with
 * `-semihosting-config` absent entirely.
 *
 * The UART stdin protocol is the ASCII magic "TCIN" (sent twice by the loader
 * to tolerate a console backend that consumes a leading byte), an eight-byte
 * little-endian length, then the payload.  The platform skips the optional
 * second magic after the first one.  It gives the platform a
 * deterministic EOF and short-read boundary; the language-level `read` sees
 * only the payload.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_platform.h"

#define TC_PL011_STDOUT_BASE UINT64_C(0x09000000)
#define TC_PL011_RECORD_BASE UINT64_C(0x09040000)
#define TC_PSCI_SYSTEM_OFF UINT64_C(0x84000008)
#define TC_UART_INPUT_MAX UINT64_C(0x00100000)

static void uart_init_at(uint64_t base) {
    volatile uint32_t *registers = (volatile uint32_t *)(uintptr_t)base;
    registers[0x24u / 4u] = 13u;   /* IBRD: 115200 at the QEMU reference clock */
    registers[0x28u / 4u] = 0u;    /* FBRD */
    registers[0x2cu / 4u] = 0x70u; /* 8 data bits, FIFO enabled, no parity */
    registers[0x30u / 4u] = 0x301u; /* UARTEN | TXE | RXE */
    registers[0x38u / 4u] = 0u;    /* mask interrupts; polling only */
    registers[0x44u / 4u] = 0x7ffu; /* clear all interrupts */
}

static void uart_putc_at(uint64_t base, char c) {
    volatile uint32_t *registers = (volatile uint32_t *)(uintptr_t)base;
    while ((registers[0x18u / 4u] & (1u << 5)) != 0) {
        /* wait while the TX FIFO is full */
    }
    registers[0] = (uint32_t)(unsigned char)c;
}

/* 1 = byte, -1 = UART error.  A program that asks for a byte blocks until the
 * platform source supplies it; the length prefix supplies EOF. */
static int uart_getc_at(uint64_t base, unsigned char *out) {
    volatile uint32_t *registers = (volatile uint32_t *)(uintptr_t)base;
    for (;;) {
        uint32_t status = registers[0x18u / 4u];
        if ((status & (1u << 4)) == 0) {
            if ((status & ((1u << 8) | (1u << 10) | (1u << 11) |
                           (1u << 12))) != 0) {
                (void)registers[0];
                registers[0x44u / 4u] = 0x7ffu;
                return -1;
            }
            *out = (unsigned char)registers[0];
            return 1;
        }
    }
}

static int stdout_uart_ready = 0;
static int input_initialized = 0;
static int input_eof = 0;
static unsigned header_length = 0;
static int length_ready = 0;
static unsigned magic_position = 0;
static int magic_checked = 0;
static unsigned char header[8];
static uint64_t remaining = 0;

int tc_rt_platform_read(char *bytes, size_t capacity, size_t *n, int *eof) {
    size_t take;
    size_t i;
    if (!input_initialized) {
        if (!stdout_uart_ready) {
            uart_init_at(TC_PL011_STDOUT_BASE);
            stdout_uart_ready = 1;
        }
        input_initialized = 1;
    }
    if (input_eof) {
        *n = 0;
        *eof = 1;
        return 0;
    }
    if (capacity == 0) {
        *n = 0;
        *eof = 0;
        return 0;
    }
    while (!length_ready) {
        unsigned char byte = 0;
        int got;
        if (magic_position < 4u) {
            static const char magic[4] = {'T', 'C', 'I', 'N'};
            got = uart_getc_at(TC_PL011_STDOUT_BASE, &byte);
            if (got < 0) {
                return 1;
            }
            if (byte == (unsigned char)magic[magic_position]) {
                magic_position++;
            } else {
                magic_position = byte == (unsigned char)'T' ? 1u : 0u;
            }
            continue;
        }
        if (!magic_checked) {
            unsigned char probe[4];
            unsigned k;
            for (k = 0; k < 4u; k++) {
                got = uart_getc_at(TC_PL011_STDOUT_BASE, &probe[k]);
                if (got < 0) {
                    return 1;
                }
            }
            if (probe[0] == 'T' && probe[1] == 'C' && probe[2] == 'I' &&
                probe[3] == 'N') {
                magic_checked = 1;
                continue;
            }
            for (k = 0; k < 4u; k++) {
                header[header_length++] = probe[k];
            }
            magic_checked = 1;
        }
        if (header_length == sizeof(header)) {
            uint64_t count = 0;
            unsigned k;
            for (k = 0; k < 8u; k++) {
                count |= (uint64_t)header[k] << (8u * k);
            }
            if (count > TC_UART_INPUT_MAX) {
                return 1;
            }
            remaining = count;
            length_ready = 1;
            break;
        }
        got = uart_getc_at(TC_PL011_STDOUT_BASE, &byte);
        if (got < 0) {
            return 1;
        }
        header[header_length++] = byte;
    }
    if (remaining == 0) {
        input_eof = 1;
        *n = 0;
        *eof = 1;
        return 0;
    }
    take = capacity;
    if (take > remaining) {
        take = remaining;
    }
    if (take > 4u) {
        take = 4u; /* exercise the standard short-read path */
    }
    for (i = 0; i < take; i++) {
        unsigned char byte = 0;
        int got = uart_getc_at(TC_PL011_STDOUT_BASE, &byte);
        if (got < 0) {
            return 1;
        }
        bytes[i] = (char)byte;
    }
    remaining -= take;
    *n = take;
    *eof = 0;
    if (remaining == 0) {
        input_eof = 1;
    }
    return 0;
}

void tc_rt_platform_stdout(const char *bytes, size_t size) {
    size_t i;
    if (!stdout_uart_ready) {
        uart_init_at(TC_PL011_STDOUT_BASE);
        stdout_uart_ready = 1;
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

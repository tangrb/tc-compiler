/* TC 0.0.46 RISC-V 32 QEMU virt platform seams (design §7.2/§8).
 *
 * Program stdout and stdin use the NS16550A UART at 0x10000000.  The stdin
 * protocol is the ASCII magic "TCIN" (sent twice by the loader to tolerate a
 * console backend that consumes a leading byte), an eight-byte little-endian
 * length, then the payload.  The exit record uses semihosting SYS_WRITE0 on a
 * separate channel from program stdout; the image terminates through the
 * SiFive test finisher at 0x100000.  The runtime is freestanding; neither the
 * loader nor the platform resolves imports.
 */
#include <stddef.h>
#include <stdint.h>

#include "backend/common/rt_platform.h"

#define TC_UART_BASE ((volatile uint8_t *)(uintptr_t)0x10000000u)
#define TC_TEST_BASE ((volatile uint32_t *)(uintptr_t)0x00100000u)
#define TC_UART_INPUT_MAX UINT64_C(0x00100000)

/* Defined in semi.S.  It makes the uncompressed RISC-V semihosting call with
 * SYS_WRITE0 and returns the semihosting result in a0. */
long tc_rv32_semihost_write0(const char *text);

static int uart_ready = 0;
static int input_initialized = 0;
static int input_eof = 0;
static unsigned header_length = 0;
static int length_ready = 0;
static unsigned magic_position = 0;
static int magic_checked = 0;
static unsigned char header[8];
static uint64_t remaining = 0;

static void uart_init(void) {
    volatile uint8_t *uart = TC_UART_BASE;
    if (uart_ready) {
        return;
    }
    uart[1] = 0x00;   /* IER: polling only */
    uart[3] = 0x80;   /* LCR: DLAB */
    uart[0] = 0x01;   /* DLL: divisor low */
    uart[1] = 0x00;   /* DLM: divisor high */
    uart[3] = 0x03;   /* LCR: 8 data bits, no parity, one stop bit */
    uart[2] = 0x07;   /* FCR: enable and clear both FIFOs */
    uart[4] = 0x03;   /* MCR: DTR | RTS */
    uart_ready = 1;
}

static void uart_putc(char c) {
    volatile uint8_t *uart = TC_UART_BASE;
    while ((uart[5] & 0x20u) == 0) {
        /* wait while the transmit holding register is full */
    }
    uart[0] = (uint8_t)(unsigned char)c;
}

/* 1 = byte, -1 = UART error.  A program that asks for a byte blocks until the
 * platform source supplies it; the length prefix supplies EOF. */
static int uart_getc(unsigned char *out) {
    volatile uint8_t *uart = TC_UART_BASE;
    for (;;) {
        uint8_t status = uart[5];
        if ((status & 0x01u) != 0) {
            *out = uart[0];
            return 1;
        }
        if ((status & 0x1eu) != 0) {
            (void)uart[0];
            return -1;
        }
    }
}

void tc_rt_platform_stdout(const char *bytes, size_t size) {
    size_t i;
    uart_init();
    for (i = 0; i < size; i++) {
        uart_putc(bytes[i]);
    }
}

int tc_rt_platform_read(char *bytes, size_t capacity, size_t *n, int *eof) {
    size_t take;
    size_t i;
    if (!input_initialized) {
        uart_init();
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
            got = uart_getc(&byte);
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
                got = uart_getc(&probe[k]);
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
        got = uart_getc(&byte);
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
    for (i = 0; i < take; i++) {
        unsigned char byte = 0;
        int got = uart_getc(&byte);
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

void tc_rt_platform_record(const char *bytes, size_t size) {
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
        (void)tc_rv32_semihost_write0(chunk);
        offset += take;
    }
}

__attribute__((noreturn)) void tc_rt_platform_exit(uint32_t status) {
    if (status == 0) {
        *TC_TEST_BASE = 0x00005555u;
    } else {
        *TC_TEST_BASE = 0x00003333u | (status << 16);
    }
    for (;;) {
    }
}

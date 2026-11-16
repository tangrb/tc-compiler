/* TC 0.0.46 freestanding AArch64 runtime (design §7.2/§10.3/§11).
 *
 * This runtime is linked into the AArch64 ELF produced by the subset backend.
 * It provides the platform seams needed by the implemented AArch64 subset:
 *   - a semihosting console for program stdout and stdin;
 *   - the first-trap diagnostic and process termination;
 *   - deterministic integer/bool formatting and token input (standard §11.2,
 *     §11.3), implemented directly so no libc is required;
 *   - the abstract call-depth counter used by generated call sequences.
 *
 * It is freestanding: no hosted headers beyond the compiler's freestanding
 * stdint/stddef, no allocation and no locale.  The diagnostic side channel is
 * the QEMU virt PL011 UART so it never mixes with the program's stdout.
 */
#include <stddef.h>
#include <stdint.h>

#include "diagnostic/codes.h"
#include "ir/type.h"

#define TC_AARCH64_PL011_BASE UINT64_C(0x09000000)

#define TC_SEMIHOST_SYS_OPEN 0x01
#define TC_SEMIHOST_SYS_CLOSE 0x02
#define TC_SEMIHOST_SYS_WRITE0 0x04
#define TC_SEMIHOST_SYS_READ 0x06
#define TC_SEMIHOST_SYS_READC 0x07
#define TC_SEMIHOST_SYS_EXIT 0x18
#define TC_SEMIHOST_SYS_EXIT_EXTENDED 0x20

#define TC_SEMIHOST_ADP_STOPPED_APPLICATION_EXIT 0x20000u

/* Abstract call depth (standard §1.2/design §10.3).  Defined in .data so the
 * loader initializes it without requiring a BSS zeroing loop. */
unsigned long __tc_call_depth = 0;

static long semihosting_call(unsigned long op, unsigned long arg) {
    register unsigned long x0 __asm__("x0") = op;
    register unsigned long x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (long)x0;
}


static void uart_init(void) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)
        TC_AARCH64_PL011_BASE;
    base[0x30u / 4u] = 0x301u; /* UARTEN | TXE | RXE */
    base[0x44u / 4u] = 0x7ffu; /* clear all interrupts */
}

static void uart_putc(char c) {
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)
        TC_AARCH64_PL011_BASE;
    while ((base[0x18u / 4u] & (1u << 5)) != 0) {
        /* wait while TX FIFO is full */
    }
    base[0] = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *text) {
    while (*text != '\0') {
        uart_putc(*text++);
    }
}

static void uart_put_uint64(unsigned long value) {
    char digits[24];
    size_t n = 0;
    if (value == 0) {
        uart_putc('0');
        return;
    }
    while (value != 0) {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n != 0) {
        uart_putc(digits[--n]);
    }
}

static const char *aarch64_trap_error_symbol(unsigned code) {
    switch ((TcDiagCode)code) {
    case TC_RE_INTEGER_OVERFLOW: return "TC_RE_INTEGER_OVERFLOW";
    case TC_RE_DIV_ZERO: return "TC_RE_DIV_ZERO";
    case TC_RE_NEGATIVE_SHIFT: return "TC_RE_NEGATIVE_SHIFT";
    case TC_RE_FLOAT_OVERFLOW: return "TC_RE_FLOAT_OVERFLOW";
    case TC_RE_FLOAT_UNDERFLOW: return "TC_RE_FLOAT_UNDERFLOW";
    case TC_RE_FLOAT_INVALID: return "TC_RE_FLOAT_INVALID";
    case TC_RE_CAST_OVERFLOW: return "TC_RE_CAST_OVERFLOW";
    case TC_RE_NULL_POINTER: return "TC_RE_NULL_POINTER";
    case TC_RE_PTRDIFF_OVERFLOW: return "TC_RE_PTRDIFF_OVERFLOW";
    case TC_RE_WRITE_PROTECTED: return "TC_RE_WRITE_PROTECTED";
    case TC_RE_ADDR_UNAVAILABLE: return "TC_RE_ADDR_UNAVAILABLE";
    case TC_RE_STACK_OVERFLOW: return "TC_RE_STACK_OVERFLOW";
    case TC_RE_NULL_FUNCTION_POINTER: return "TC_RE_NULL_FUNCTION_POINTER";
    case TC_RE_IO: return "TC_RE_IO";
    case TC_RE_TRAP: return "TC_RE_TRAP";
    default: return "TC_RE_TRAP";
    }
}

__attribute__((noreturn)) static void semihosting_exit(unsigned status) {
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

/* Called from _start after tc_main returns. */
void tc_aarch64_runtime_exit(unsigned status) {
    uart_init();
    uart_puts("TC-EXIT ");
    uart_put_uint64(status);
    uart_puts(" none\n");
    semihosting_exit(status);
}

__attribute__((noreturn)) void tc_aarch64_trap(unsigned code, unsigned line,
                                       unsigned column, const char *file) {
    uart_init();
    uart_puts("error[RT]: ");
    uart_puts(aarch64_trap_error_symbol(code));
    uart_puts(" at ");
    uart_puts(file != NULL ? file : "<source>");
    uart_putc(':');
    uart_put_uint64(line);
    uart_putc(':');
    uart_put_uint64(column);
    uart_putc('\n');
    uart_puts("TC-EXIT ");
    uart_put_uint64(1u);
    uart_putc(' ');
    uart_puts(aarch64_trap_error_symbol(code));
    uart_putc('\n');
    semihosting_exit(1u);
}

/* ------------------------------------------------------------------------- */
/* Deterministic integer/bool formatting (§11.2).                             */
/* ------------------------------------------------------------------------- */

static int format_bool(uint64_t bits, char *out, size_t cap) {
    const char *text = (bits & 0xffu) != 0 ? "true" : "false";
    size_t len = 0;
    while (text[len] != '\0') {
        len++;
    }
    if (len + 1u > cap) {
        return 0;
    }
    for (size_t i = 0; i <= len; i++) {
        out[i] = text[i];
    }
    return 1;
}

static unsigned aarch64_type_width(unsigned tag) {
    switch ((TcTypeTag)tag) {
    case TC_TY_I8:
    case TC_TY_U8:
    case TC_TY_BOOL:
        return 8;
    case TC_TY_I16:
    case TC_TY_U16:
        return 16;
    case TC_TY_I32:
    case TC_TY_U32:
        return 32;
    case TC_TY_I64:
    case TC_TY_U64:
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
        return 64;
    default:
        return 0;
    }
}

static int aarch64_type_is_signed(unsigned tag) {
    switch ((TcTypeTag)tag) {
    case TC_TY_I8:
    case TC_TY_I16:
    case TC_TY_I32:
    case TC_TY_I64:
    case TC_TY_ISIZE:
        return 1;
    default:
        return 0;
    }
}

static uint64_t aarch64_width_mask(unsigned width) {
    return width >= 64u ? UINT64_MAX : ((UINT64_C(1) << width) - 1u);
}

static int format_unsigned(uint64_t value, char *out, size_t cap,
                           size_t *out_len) {
    char digits[24];
    size_t n = 0;
    size_t i;
    if (value == 0) {
        digits[n++] = '0';
    }
    while (value != 0) {
        digits[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    if (n + 1u > cap) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        out[i] = digits[n - 1u - i];
    }
    out[n] = '\0';
    *out_len = n;
    return 1;
}

int tc_aarch64_write_scalar(unsigned tag, uint64_t bits, unsigned newline) {
    char buffer[64];
    size_t len = 0;
    if ((TcTypeTag)tag == TC_TY_BOOL) {
        if (!format_bool(bits, buffer, sizeof(buffer))) {
            return 1;
        }
        while (buffer[len] != '\0') {
            len++;
        }
    } else {
        unsigned width = aarch64_type_width(tag);
        uint64_t mask;
        if (width == 0) {
            return 1;
        }
        mask = aarch64_width_mask(width);
        bits &= mask;
        if (aarch64_type_is_signed(tag)) {
            uint64_t sign = UINT64_C(1) << (width - 1u);
            if ((bits & sign) != 0) {
                buffer[len++] = '-';
                {
                    size_t written = 0;
                    if (!format_unsigned((UINT64_C(0) - bits) & mask,
                                         buffer + len, sizeof(buffer) - len,
                                         &written)) {
                        return 1;
                    }
                    len += written;
                }
            } else if (!format_unsigned(bits, buffer, sizeof(buffer),
                                        &len)) {
                return 1;
            }
        } else if (!format_unsigned(bits, buffer, sizeof(buffer), &len)) {
            return 1;
        }
    }
    if (newline != 0) {
        if (len + 2u > sizeof(buffer)) {
            return 1;
        }
        buffer[len++] = '\n';
        buffer[len] = '\0';
    } else {
        buffer[len] = '\0';
    }
    if (len != 0) {
        /* The semihosting console has no separate failure status; the call
         * either writes the record or the platform terminates.  A single call
         * keeps the record all-or-nothing. */
        (void)semihosting_call(TC_SEMIHOST_SYS_WRITE0,
                            (unsigned long)(uintptr_t)buffer);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Deterministic integer/bool input (§11.3).                                  */
/* ------------------------------------------------------------------------- */

/* Input image for the QEMU platform instance.  The test loader places an
 * 8-byte little-endian length followed by the bytes at TC_INPUT_IMAGE_BASE;
 * an absent loader leaves the RAM zero, which is an empty input stream.  This
 * keeps EOF and short reads observable without depending on host console EOF
 * behavior.  PL011 input is a later freestanding-platform seam. */
#define TC_INPUT_IMAGE_BASE UINT64_C(0x41000000)
#define TC_INPUT_IMAGE_MAX UINT64_C(0x00100000)

static int input_loaded = 0;
static const uint8_t *input_data = NULL;
static size_t input_len = 0;
static size_t input_pos = 0;
static int input_pending = -2; /* -2: none, -1: EOF, otherwise a byte */

static const uint8_t *input_image_base(void) {
    return (const uint8_t *)(uintptr_t)TC_INPUT_IMAGE_BASE;
}

static void input_load(void) {
    const uint8_t *base;
    uint64_t n = 0;
    unsigned i;
    if (input_loaded) {
        return;
    }
    base = input_image_base();
    for (i = 0; i < 8u; i++) {
        n |= (uint64_t)base[i] << (8u * i);
    }
    if (n > TC_INPUT_IMAGE_MAX) {
        n = 0;
    }
    input_data = base + 8;
    input_len = (size_t)n;
    input_pos = 0;
    input_loaded = 1;
}

static int input_get(void) {
    if (input_pending != -2) {
        int value = input_pending;
        input_pending = -2;
        return value;
    }
    input_load();
    if (input_pos >= input_len) {
        return -1;
    }
    return (int)input_data[input_pos++];
}

static void input_unget(int c) {
    input_pending = c;
}

static int is_ascii_space(int c) {
    return c == 0x09 || c == 0x0a || c == 0x0b || c == 0x0c || c == 0x0d ||
           c == 0x20;
}

static int token_equals(const char *token, size_t len, const char *word) {
    size_t i = 0;
    while (word[i] != '\0') {
        i++;
    }
    if (i != len) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (token[i] != word[i]) {
            return 0;
        }
    }
    return 1;
}

static int parse_digits(const char *token, size_t len, uint64_t limit,
                        uint64_t *out) {
    uint64_t value = 0;
    size_t i;
    if (len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned digit;
        if (token[i] < '0' || token[i] > '9') {
            return 0;
        }
        digit = (unsigned)(token[i] - '0');
        if (value > (limit - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

int tc_aarch64_read_scalar(unsigned tag, uint64_t *out) {
    char token[96];
    size_t len = 0;
    int c;
    int negative = 0;
    int leading_zero = 0;
    int is_bool = (TcTypeTag)tag == TC_TY_BOOL;
    unsigned width;
    uint64_t limit;
    uint64_t magnitude;
    if (out == NULL) {
        return 1;
    }
    width = aarch64_type_width(tag);
    if (width == 0) {
        return 1;
    }
    do {
        c = input_get();
    } while (c >= 0 && is_ascii_space(c));
    if (c < 0) {
        return 1;
    }
    if (c == '-') {
        if (!aarch64_type_is_signed(tag)) {
            return 1;
        }
        negative = 1;
        c = input_get();
        if (c < 0) {
            return 1;
        }
    }
    while (c >= 0 && !is_ascii_space(c)) {
        /* Leading zeros do not affect the value and input tokens allow any
         * number of them; store only the significant part. */
        if (!is_bool && len == 0 && c == '0') {
            leading_zero = 1;
            c = input_get();
            continue;
        }
        if (len + 1u >= sizeof(token)) {
            /* Consume the rest of this token, then fail deterministically. */
            while (c >= 0 && !is_ascii_space(c)) {
                c = input_get();
            }
            return 1;
        }
        token[len++] = (char)c;
        c = input_get();
    }
    if (c >= 0) {
        input_unget(c);
    }
    if (is_bool) {
        if (negative) {
            return 1;
        }
        if (token_equals(token, len, "true")) {
            *out = 1;
            return 0;
        }
        if (token_equals(token, len, "false")) {
            *out = 0;
            return 0;
        }
        return 1;
    }
    if (len == 0 && leading_zero) {
        token[len++] = '0';
    }
    if (negative) {
        limit = UINT64_C(1) << (width - 1u);
    } else if (aarch64_type_is_signed(tag)) {
        limit = (UINT64_C(1) << (width - 1u)) - 1u;
    } else {
        limit = aarch64_width_mask(width);
    }
    if (!parse_digits(token, len, limit, &magnitude)) {
        return 1;
    }
    if (negative) {
        *out = (UINT64_C(0) - magnitude) & aarch64_width_mask(width);
    } else {
        *out = magnitude & aarch64_width_mask(width);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Freestanding memory helpers.                                               */
/* ------------------------------------------------------------------------- */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        for (i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (i = n; i != 0; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
    return dst;
}

void *memset(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }
    return dst;
}

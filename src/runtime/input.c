/* TC 0.0.46 standard input token reader. */
#include "runtime/input.h"

#include "runtime/rt_memory.h"

#include "frontend/input.h" /* arena used by the exact decimal converter */
#include "frontend/num.h"
#include "runtime/semantics.h"

static int is_space(uint8_t byte) {
    return byte == 0x09u || byte == 0x0au || byte == 0x0bu || byte == 0x0cu ||
           byte == 0x0du || byte == 0x20u;
}

void tc_input_init(TcInput *input, const TcIoSrc *src,
                   const TcAllocator *alloc) {
    if (input == NULL) {
        return;
    }
    tc_rt_memset(input, 0, sizeof(*input));
    input->src = src;
    input->alloc = alloc != NULL ? alloc : tc_allocator_libc();
    input->token_cap = 64;
    input->token = (char *)input->alloc->alloc(input->alloc->ctx,
                                               input->token_cap);
    if (input->token != NULL) {
        input->token[0] = '\0';
    }
}

void tc_input_destroy(TcInput *input) {
    if (input == NULL) {
        return;
    }
    if (input->token != NULL && input->alloc != NULL &&
        input->alloc->free != NULL) {
        input->alloc->free(input->alloc->ctx, input->token);
    }
    if (input->replay != NULL && input->alloc != NULL &&
        input->alloc->free != NULL) {
        input->alloc->free(input->alloc->ctx, input->replay);
    }
    input->token = NULL;
    input->token_cap = 0;
    input->token_len = 0;
    input->replay = NULL;
    input->replay_cap = 0;
    input->replay_len = 0;
    input->replay_pos = 0;
}

static int consume_byte(TcInput *input, uint8_t *out) {
    if (input->consumed == UINT64_MAX) {
        input->consumed_overflow = 1;
        input->error = 1;
        return -1;
    }
    input->consumed++;
    if (input->replay_pos < input->replay_len) {
        *out = (uint8_t)input->replay[input->replay_pos++];
    } else {
        *out = input->buffer[input->pos++];
    }
    return 1;
}

/* Fetch one byte.  Returns 1 with the byte, 0 at EOF, -1 at source error. */
static int next_byte(TcInput *input, uint8_t *out) {
    if (input->replay_pos < input->replay_len) {
        return consume_byte(input, out);
    }
    if (input->pos < input->len) {
        return consume_byte(input, out);
    }
    if (input->eof || input->error || input->src == NULL ||
        input->src->read == NULL) {
        return input->error ? -1 : 0;
    }
    while (input->pos >= input->len) {
        size_t n = 0;
        TcIoStatus status = TC_IO_OK;
        int rc = input->src->read(input->src->ctx, input->buffer,
                                  sizeof(input->buffer), &n, &status);
        input->pos = 0;
        input->len = 0;
        if (status == TC_IO_AGAIN && rc == 0 && n == 0u) {
            return -2;
        }
        if (rc != 0 || status == TC_IO_ERROR) {
            input->error = 1;
            return -1;
        }
        if (status == TC_IO_EOF) {
            input->eof = 1;
        }
        if (n > sizeof(input->buffer)) {
            input->error = 1;
            return -1;
        }
        input->len = n;
        if (n == 0) {
            return 0;
        }
    }
    return consume_byte(input, out);
}

static TcInputStatus append_token(TcInput *input, char byte) {
    if (input->token == NULL) {
        return TC_INPUT_OOM;
    }
    if (input->token_len + 1u >= input->token_cap) {
        size_t cap = input->token_cap * 2u;
        char *fresh = (char *)input->alloc->realloc(
            input->alloc->ctx, input->token, cap);
        if (fresh == NULL) {
            return TC_INPUT_OOM;
        }
        input->token = fresh;
        input->token_cap = cap;
    }
    input->token[input->token_len++] = byte;
    input->token[input->token_len] = '\0';
    return TC_INPUT_OK;
}

static int parse_unsigned_digits(const char *digits, size_t n, uint64_t limit,
                                 uint64_t *out) {
    uint64_t value = 0;
    size_t i;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned digit;
        if (digits[i] < '0' || digits[i] > '9') {
            return 0;
        }
        digit = (unsigned)(digits[i] - '0');
        if (value > limit / 10u) {
            return 0;
        }
        value *= 10u;
        if (value > limit - digit) {
            return 0;
        }
        value += digit;
    }
    *out = value;
    return 1;
}

static int parse_integer_token(const char *token, size_t n, unsigned width,
                               int is_signed, uint64_t *out) {
    uint64_t mask = tc_sem_mask(width);
    uint64_t limit;
    uint64_t magnitude;
    int negative = 0;
    size_t start = 0;

    if (n == 0) {
        return 0;
    }
    if (token[0] == '-') {
        if (!is_signed || n == 1) {
            return 0;
        }
        negative = 1;
        start = 1;
    }
    if (is_signed) {
        limit = negative ? (UINT64_C(1) << (width - 1u))
                         : ((UINT64_C(1) << (width - 1u)) - 1u);
    } else {
        limit = mask;
    }
    if (!parse_unsigned_digits(token + start, n - start, limit, &magnitude)) {
        return 0;
    }
    if (negative) {
        magnitude = (UINT64_C(0) - magnitude) & mask;
    }
    *out = magnitude & mask;
    return 1;
}

/* Parse a decimal floating token.  Returns 1 on success, 0 for an invalid
 * token or an out-of-range value, and -1 on allocation failure. */
static int parse_float_token(TcInput *input, const char *token, size_t n,
                             unsigned width, uint64_t *out) {
    char small[128];
    char *digits = small;
    size_t ndigits = 0;
    size_t start = 0;
    size_t before = 0;
    size_t after = 0;
    int negative = 0;
    int exponent = 0;
    int exponent_negative = 0;
    size_t i;
    uint64_t bits = 0;
    TcFloatRange range = TC_FLOAT_OK;
    int rc;
    int ok = 0;

    if (n == 3 && tc_rt_memcmp(token, "inf", 3) == 0) {
        *out = tc_sem_inf_bits(width, 0);
        return 1;
    }
    if (n == 4 && tc_rt_memcmp(token, "-inf", 4) == 0) {
        *out = tc_sem_inf_bits(width, 1);
        return 1;
    }
    if (n == 3 && tc_rt_memcmp(token, "nan", 3) == 0) {
        *out = tc_sem_nan_bits(width);
        return 1;
    }
    if (n > sizeof(small)) {
        digits = (char *)input->alloc->alloc(input->alloc->ctx, n);
        if (digits == NULL) {
            return -1;
        }
    }
    if (token[0] == '-') {
        negative = 1;
        start = 1;
        if (n == 1) {
            goto done;
        }
    }
    i = start;
    while (i < n && token[i] >= '0' && token[i] <= '9') {
        digits[ndigits++] = token[i++];
        before++;
    }
    if (before == 0) {
        goto done;
    }
    if (i < n && token[i] == '.') {
        i++;
        while (i < n && token[i] >= '0' && token[i] <= '9') {
            digits[ndigits++] = token[i++];
            after++;
        }
        if (after == 0) {
            goto done;
        }
    }
    if (i < n && (token[i] == 'e' || token[i] == 'E')) {
        i++;
        if (i < n && token[i] == '-') {
            exponent_negative = 1;
            i++;
        } else if (i < n && token[i] == '+') {
            goto done;
        }
        if (i >= n) {
            goto done;
        }
        while (i < n && token[i] >= '0' && token[i] <= '9') {
            if (exponent > 100000) {
                goto done;
            }
            exponent = exponent * 10 + (int)(token[i] - '0');
            i++;
        }
        if (i != n) {
            goto done;
        }
    } else if (i != n) {
        goto done;
    }
    if (exponent_negative) {
        exponent = -exponent;
    }
    exponent -= (int)after;
    rc = tc_decimal_to_float(digits, ndigits, exponent, negative, width, &bits,
                            &range);
    if (rc != 0 || range != TC_FLOAT_OK) {
        goto done;
    }
    *out = bits & tc_sem_mask(width);
    ok = 1;
done:
    if (digits != small) {
        input->alloc->free(input->alloc->ctx, digits);
    }
    return ok;
}

static TcInputStatus read_scalar_impl(TcInput *input, unsigned W,
                                      const TcType *type, uint64_t *bits) {
    int saw_any = 0;

    if (input == NULL || type == NULL || bits == NULL ||
        input->token == NULL) {
        return TC_INPUT_OOM;
    }
    input->token_len = 0;
    input->token[0] = '\0';
    input->token_committed = 0;
    for (;;) {
        uint8_t byte;
        int rc = next_byte(input, &byte);
        if (rc == -2) {
            return TC_INPUT_PAUSE;
        }
        if (rc < 0) {
            return TC_INPUT_IO;
        }
        if (rc == 0) {
            if (!saw_any) {
                return TC_INPUT_IO;
            }
            break;
        }
        if (!saw_any && is_space(byte)) {
            continue;
        }
        if (saw_any && is_space(byte)) {
            break;
        }
        {
            TcInputStatus status = append_token(input, (char)byte);
            if (status != TC_INPUT_OK) {
                return status;
            }
        }
        saw_any = 1;
    }
    if (input->token_len == 0) {
        return TC_INPUT_IO;
    }
    if (type->tag == TC_TY_BOOL) {
        if (input->token_len == 4 &&
            tc_rt_memcmp(input->token, "true", 4) == 0) {
            *bits = 1;
            input->token_committed = 1;
            return TC_INPUT_OK;
        }
        if (input->token_len == 5 &&
            tc_rt_memcmp(input->token, "false", 5) == 0) {
            *bits = 0;
            input->token_committed = 1;
            return TC_INPUT_OK;
        }
        return TC_INPUT_IO;
    }
    if (type->tag == TC_TY_F32 || type->tag == TC_TY_F64) {
        int rc = parse_float_token(input, input->token, input->token_len,
                                   type->tag == TC_TY_F32 ? 32u : 64u, bits);
        if (rc < 0) {
            return TC_INPUT_OOM;
        }
        if (rc == 1) {
            input->token_committed = 1;
            return TC_INPUT_OK;
        }
        return TC_INPUT_IO;
    }
    if (!tc_type_is_integer(type)) {
        return TC_INPUT_IO;
    }
    {
        unsigned width = tc_sem_type_width(type, W);
        if (width == 0 ||
            !parse_integer_token(input->token, input->token_len, width,
                                 tc_type_is_signed_integer(type), bits)) {
            return TC_INPUT_IO;
        }
    }
    input->token_committed = 1;
    return TC_INPUT_OK;
}

TcInputStatus tc_input_read_scalar(TcInput *input, unsigned W,
                                   const TcType *type, uint64_t *bits) {
    TcInputStatus status;
    uint64_t start = input != NULL ? input->consumed : 0u;
    if (input != NULL) {
        input->last_consumed = 0u;
        input->last_eof = 0;
    }
    status = read_scalar_impl(input, W, type, bits);
    if (input != NULL) {
        if (status == TC_INPUT_OK) {
            input->last_consumed = input->consumed - start;
        } else {
            input->last_eof = status == TC_INPUT_IO && input->eof &&
                              !input->error && input->token_len == 0u;
        }
    }
    return status;
}

int tc_input_restart(TcInput *input) {
    size_t replay_remaining;
    size_t buffer_remaining;
    size_t token_remaining;
    size_t combined;
    size_t offset = 0u;
    char *replay;
    if (input == NULL) {
        return -1;
    }
    replay_remaining = input->replay != NULL
                           ? input->replay_len - input->replay_pos
                           : 0u;
    buffer_remaining = input->len - input->pos;
    token_remaining = !input->token_committed ? input->token_len : 0u;
    if (replay_remaining > SIZE_MAX - buffer_remaining ||
        token_remaining > SIZE_MAX - replay_remaining - buffer_remaining) {
        return -1;
    }
    combined = token_remaining + replay_remaining + buffer_remaining;
    if (combined != 0u) {
        replay = (char *)input->alloc->alloc(input->alloc->ctx, combined);
        if (replay == NULL) {
            return -1;
        }
        if (token_remaining != 0u) {
            tc_rt_memcpy(replay, input->token, token_remaining);
            offset = token_remaining;
        }
        if (replay_remaining != 0u) {
            tc_rt_memcpy(replay + offset,
                         input->replay + input->replay_pos, replay_remaining);
            offset += replay_remaining;
        }
        if (buffer_remaining != 0u) {
            tc_rt_memcpy(replay + offset, input->buffer + input->pos,
                         buffer_remaining);
        }
        if (input->replay != NULL) {
            input->alloc->free(input->alloc->ctx, input->replay);
        }
        input->replay = replay;
        input->replay_cap = combined;
        input->replay_len = combined;
    } else {
        input->replay_len = 0u;
    }
    input->replay_pos = 0u;
    input->pos = 0u;
    input->len = 0u;
    input->token_len = 0;
    if (input->token != NULL) {
        input->token[0] = '\0';
    }
    input->token_committed = 0;
    input->consumed = 0;
    input->last_consumed = 0;
    input->last_eof = 0;
    input->consumed_overflow = 0;
    return 0;
}

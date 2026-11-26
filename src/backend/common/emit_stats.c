#include "backend/common/emit_stats.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int word_at(const char *cursor, const char *end, const char *word) {
    size_t length = strlen(word);
    return (size_t)(end - cursor) >= length &&
           strncmp(cursor, word, length) == 0 &&
           ((size_t)(end - cursor) == length ||
            cursor[length] == ' ' || cursor[length] == '\t' ||
            cursor[length] == ',');
}

static const char *skip_space(const char *cursor, const char *end) {
    while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
        cursor++;
    }
    return cursor;
}

static void add_bytes(TcEmitCounter *counter, uint64_t amount) {
    if (counter->code_bytes > UINT64_MAX - amount) {
        counter->code_bytes = UINT64_MAX;
    } else {
        counter->code_bytes += amount;
    }
}

static void align_text(TcEmitCounter *counter, const char *cursor,
                       const char *end) {
    char *parse_end = NULL;
    unsigned long exponent;
    uint64_t alignment = 1u;
    uint64_t padding;
    unsigned i;
    cursor = skip_space(cursor, end);
    if (cursor == end || *cursor < '0' || *cursor > '9') {
        return;
    }
    exponent = strtoul(cursor, &parse_end, 10);
    if (parse_end == cursor || parse_end > end || exponent > 20u) {
        return;
    }
    for (i = 0u; i < (unsigned)exponent; i++) {
        alignment <<= 1;
    }
    padding = (alignment - counter->code_bytes % alignment) % alignment;
    add_bytes(counter, padding);
}

static void update_section(TcEmitCounter *counter, const char *cursor,
                           const char *end) {
    const char *name;
    size_t length;
    cursor = skip_space(cursor, end);
    if (word_at(cursor, end, ".text")) {
        counter->in_text = 1;
        return;
    }
    if (!word_at(cursor, end, ".section")) {
        return;
    }
    cursor = skip_space(cursor + strlen(".section"), end);
    name = cursor;
    while (cursor < end && *cursor != ',' && *cursor != ' ' &&
           *cursor != '\t') {
        cursor++;
    }
    length = (size_t)(cursor - name);
    if (length >= 2u && name[0] == '"' && name[length - 1u] == '"') {
        name++;
        length -= 2u;
    }
    counter->in_text = length == 5u && strncmp(name, ".text", 5u) == 0;
}

static void process_line(const char *line, size_t length,
                         TcEmitCounter *counter) {
    const char *end = line + length;
    const char *cursor = skip_space(line, end);
    if (cursor < end && *cursor == '.') {
        if (word_at(cursor, end, ".section") ||
            word_at(cursor, end, ".text")) {
            update_section(counter, cursor, end);
        } else if (counter->in_text &&
                   word_at(cursor, end, ".p2align")) {
            align_text(counter, cursor + strlen(".p2align"), end);
        }
    } else if (counter->in_text && cursor < end && *cursor != '#' &&
               *cursor != '@' && *cursor != '/') {
        const char *last = end;
        uint64_t width = 4u;
        while (last > cursor && (last[-1] == ' ' || last[-1] == '\t' ||
                                 last[-1] == '\r')) {
            last--;
        }
        if (last > cursor && last[-1] != ':') {
            if (counter->target == TC_EMIT_TARGET_RISCV32 &&
                (word_at(cursor, last, "call") ||
                 word_at(cursor, last, "tail"))) {
                width = 8u;
            }
            add_bytes(counter, width);
        }
    }
}

void tc_emit_stats_free(TcEmitCounter *counter) {
    if (counter == NULL) {
        return;
    }
    free(counter->pending);
    counter->pending = NULL;
    counter->pending_size = 0u;
    counter->pending_capacity = 0u;
}

static int append_pending(TcEmitCounter *counter, const char *text,
                          size_t length) {
    size_t needed;
    char *pending;
    if (length > SIZE_MAX - counter->pending_size) {
        tc_emit_stats_free(counter);
        return -2;
    }
    needed = counter->pending_size + length;
    if (needed > counter->pending_capacity) {
        size_t capacity = counter->pending_capacity == 0u
                              ? 256u : counter->pending_capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2u) {
                capacity = needed;
                break;
            }
            capacity *= 2u;
        }
        pending = (char *)realloc(counter->pending, capacity);
        if (pending == NULL) {
            tc_emit_stats_free(counter);
            return -2;
        }
        counter->pending = pending;
        counter->pending_capacity = capacity;
    }
    memcpy(counter->pending + counter->pending_size, text, length);
    counter->pending_size = needed;
    return 0;
}

static int consume_text(TcEmitCounter *counter, const char *text) {
    while (*text != '\0') {
        const char *end = strchr(text, '\n');
        size_t length = end != NULL ? (size_t)(end - text) : strlen(text);
        if (counter->pending_size == 0u && end != NULL) {
            process_line(text, length, counter);
        } else {
            if (append_pending(counter, text, length) != 0) {
                return -2;
            }
            if (end != NULL) {
                process_line(counter->pending, counter->pending_size, counter);
                counter->pending_size = 0u;
            }
        }
        text = end != NULL ? end + 1 : text + length;
    }
    return 0;
}

int tc_emit_stats_write(FILE *out, const char *format, va_list args,
                        TcEmitCounter *counter) {
    char local[4096];
    char *text = local;
    va_list copy;
    int length;
    int written;
    if (out == NULL || format == NULL) {
        return -3;
    }
    if (counter == NULL || !counter->active) {
        return vfprintf(out, format, args) < 0 ? -1 : 0;
    }
    va_copy(copy, args);
    length = vsnprintf(local, sizeof(local), format, copy);
    va_end(copy);
    if (length < 0) {
        return -3;
    }
    if ((size_t)length >= sizeof(local)) {
        text = (char *)malloc((size_t)length + 1u);
        if (text == NULL) {
            return -2;
        }
        va_copy(copy, args);
        written = vsnprintf(text, (size_t)length + 1u, format, copy);
        va_end(copy);
        if (written != length) {
            free(text);
            return -3;
        }
    }
    written = consume_text(counter, text);
    if (written == 0) {
        written = fputs(text, out) == EOF ? -1 : 0;
    }
    if (text != local) {
        free(text);
    }
    return written;
}

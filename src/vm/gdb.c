#include "vm/gdb.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "diagnostic/diagnostic.h"
#include "ir/print.h"
#include "runtime/input.h"
#include "runtime/semantics.h"
#include "vm/debug_elf.h"
#include "vm/gdb_journal.h"
#include "vm/gdb_rsp.h"
#include "vm/internal.h"

typedef enum {
    TC_GDB_STOPPED = 0,
    TC_GDB_RUNNING,
    TC_GDB_TERMINATED
} TcGdbState;

typedef enum {
    TC_GDB_MONITOR_NONE = 0,
    TC_GDB_MONITOR_NEXT,
    TC_GDB_MONITOR_FINISH
} TcGdbMonitorMode;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int failed;
} TcGdbText;

typedef struct {
    uint64_t address;
    size_t size;
    uint8_t *bytes;
} TcGdbHeapPatch;

typedef struct TcGdbSession {
    TcVm *vm;
    const TcGdbOptions *options;
    const TcPlatformProfile *profile;
    TcPlatformProfile local_profile;
    TcIo local_io;
    TcIoSink local_out;
    TcIoSrc local_in;
    TcVmDebugHooks hooks;
    TcGdbJournal *journal;
    int listener;
    int client;
    int no_ack;
    int extended;
    int connected;
    int closing;
    int exiting;
    int detached;
    int fatal_io;
    int fatal_oom;
    int bad_checksums;
    int discarding;
    int stop_reason_interrupt;
    int suppress_trap_reply;
    int input_paused;
    int skip_breakpoint;
    uint64_t skip_address;
    uint64_t breakpoints[64];
    size_t nbreakpoints;
    TcGdbState state;
    TcGdbMonitorMode monitor_mode;
    uint32_t monitor_frame;
    uint64_t monitor_target_pc;
    int monitor_have_target;
    uint8_t rx[8192];
    size_t rx_len;
    TcGdbHeapPatch *heap_patches;
    size_t nheap_patches;
    size_t cap_heap_patches;
    uint8_t frame[TC_GDB_RSP_PACKET_SIZE * 2u + 8u];
    size_t frame_len;
    size_t frame_hash;
    int thread_listed;
    uint64_t port;
} TcGdbSession;

static volatile sig_atomic_t tc_gdb_host_interrupt;

static void host_interrupt(int signo) {
    (void)signo;
    tc_gdb_host_interrupt = 1;
}

static int text_reserve(TcGdbText *text, size_t extra) {
    size_t needed;
    size_t cap;
    uint8_t *fresh;
    if (text->failed) {
        return 0;
    }
    if (extra > SIZE_MAX - text->len - 1u) {
        text->failed = 1;
        return 0;
    }
    needed = text->len + extra + 1u;
    if (needed <= text->cap) {
        return 1;
    }
    cap = text->cap != 0u ? text->cap : 256u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    fresh = (uint8_t *)realloc(text->data, cap);
    if (fresh == NULL) {
        text->failed = 1;
        return 0;
    }
    text->data = fresh;
    text->cap = cap;
    return 1;
}

static int text_append(TcGdbText *text, const void *bytes, size_t size) {
    if (!text_reserve(text, size)) {
        return 0;
    }
    if (size != 0u) {
        memcpy(text->data + text->len, bytes, size);
    }
    text->len += size;
    text->data[text->len] = 0u;
    return 1;
}

static int text_puts(TcGdbText *text, const char *value) {
    return text_append(text, value, strlen(value));
}

static int text_printf(TcGdbText *text, const char *format, ...) {
    va_list args;
    va_list copy;
    int count;
    int ok;
    va_start(args, format);
    va_copy(copy, args);
    count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    ok = count >= 0 && text_reserve(text, (size_t)count);
    if (ok) {
        vsnprintf((char *)text->data + text->len, text->cap - text->len,
                  format, args);
        text->len += (size_t)count;
    }
    va_end(args);
    return ok;
}

static void text_dispose(TcGdbText *text) {
    free(text->data);
    memset(text, 0, sizeof(*text));
}

static void heap_patches_clear(TcGdbSession *session) {
    size_t i;
    for (i = 0u; i < session->nheap_patches; i++) {
        free(session->heap_patches[i].bytes);
    }
    free(session->heap_patches);
    session->heap_patches = NULL;
    session->nheap_patches = 0u;
    session->cap_heap_patches = 0u;
}

static int heap_write(TcGdbSession *session, uint64_t address,
                      const uint8_t *bytes, size_t size) {
    TcGdbHeapPatch *fresh;
    uint8_t *copy;
    if (size == 0u) {
        return 1;
    }
    if (session->nheap_patches == session->cap_heap_patches) {
        size_t cap = session->cap_heap_patches == 0u ? 8u
                                                     : session->cap_heap_patches * 2u;
        if (cap < session->cap_heap_patches ||
            cap > SIZE_MAX / sizeof(*fresh)) {
            return 0;
        }
        fresh = (TcGdbHeapPatch *)realloc(session->heap_patches,
                                          cap * sizeof(*fresh));
        if (fresh == NULL) {
            return 0;
        }
        session->heap_patches = fresh;
        session->cap_heap_patches = cap;
    }
    copy = (uint8_t *)malloc(size);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, bytes, size);
    session->heap_patches[session->nheap_patches].address = address;
    session->heap_patches[session->nheap_patches].size = size;
    session->heap_patches[session->nheap_patches].bytes = copy;
    session->nheap_patches++;
    return 1;
}

static void heap_read_overlay(const TcGdbSession *session, uint64_t address,
                              uint8_t *bytes, size_t size) {
    size_t i;
    for (i = 0u; i < session->nheap_patches; i++) {
        const TcGdbHeapPatch *patch = &session->heap_patches[i];
        uint64_t patch_end = patch->address + (uint64_t)patch->size;
        uint64_t read_end = address + (uint64_t)size;
        uint64_t start = address > patch->address ? address : patch->address;
        uint64_t end = read_end < patch_end ? read_end : patch_end;
        if (start < end) {
            memcpy(bytes + (size_t)(start - address),
                   patch->bytes + (size_t)(start - patch->address),
                   (size_t)(end - start));
        }
    }
}

static int send_all(TcGdbSession *session, const uint8_t *bytes, size_t size) {
    size_t sent = 0u;
    while (sent < size) {
        ssize_t count = send(session->client, bytes + sent, size - sent,
                             MSG_NOSIGNAL);
        if (count > 0) {
            sent += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            if (tc_gdb_host_interrupt) {
                session->exiting = 1;
            }
            continue;
        }
        session->connected = 0;
        session->closing = 1;
        return 0;
    }
    return 1;
}

static int send_ack(TcGdbSession *session, char value) {
    uint8_t byte = (uint8_t)value;
    if (session->no_ack) {
        return 1;
    }
    return send_all(session, &byte, 1u);
}

static int send_packet(TcGdbSession *session, const uint8_t *data, size_t len) {
    uint8_t *wire;
    size_t wire_len;
    int ok;
    if (!session->connected || session->client < 0) {
        return 0;
    }
    wire = (uint8_t *)malloc(len * 2u + 4u);
    if (wire == NULL) {
        session->fatal_oom = 1;
        return 0;
    }
    wire_len = tc_gdb_rsp_frame(data, len, wire, len * 2u + 4u);
    ok = wire_len != 0u && send_all(session, wire, wire_len);
    free(wire);
    return ok;
}

static int send_text(TcGdbSession *session, const char *text) {
    return send_packet(session, (const uint8_t *)text, strlen(text));
}

static int send_error(TcGdbSession *session) {
    return send_text(session, "E01");
}

static int fail_oom(TcGdbSession *session) {
    session->fatal_oom = 1;
    session->exiting = 1;
    session->closing = 1;
    return 0;
}

static int send_ok(TcGdbSession *session) {
    return send_text(session, "OK");
}

static const char *region_name(TcRegionKind region) {
    static const char *const names[TC_REGION_COUNT] = {
        "frame", "static", "device", "code", "heap"
    };
    return (unsigned)region < TC_REGION_COUNT ? names[region] : "?";
}

static int arch_kind(const TcPlatformProfile *profile) {
    const char *name = profile->target != NULL ? profile->target->name : "";
    if (strcmp(name, "arm32-baremetal") == 0) {
        return 1;
    }
    if (strcmp(name, "riscv32-baremetal") == 0) {
        return 2;
    }
    return 0;
}

static unsigned core_reg_count(int arch) {
    return arch == 1 ? 17u : arch == 2 ? 33u : 34u;
}

static unsigned arch_pc_reg(int arch) {
    return arch == 1 ? 15u : 32u;
}

static unsigned tc_reg_base(int arch) {
    return core_reg_count(arch);
}

static unsigned core_reg_bits(int arch, unsigned regnum) {
    return arch == 0 && regnum == 33u ? 32u : arch == 0 ? 64u : 32u;
}

static int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int parse_hex(const char *text, size_t len, uint64_t *out) {
    uint64_t value = 0u;
    size_t i;
    if (len == 0u) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        int digit = hex_digit_value(text[i]);
        if (digit < 0 || value > (UINT64_MAX - (uint64_t)digit) / 16u) {
            return 0;
        }
        value = value * 16u + (uint64_t)digit;
    }
    *out = value;
    return 1;
}

static int parse_decimal(const char *text, size_t len, uint64_t *out) {
    uint64_t value = 0u;
    size_t i;
    if (len == 0u) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        digit = (unsigned)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

static int payload_hex(const uint8_t *data, size_t len, uint64_t *out) {
    size_t i;
    uint64_t value = 0u;
    if (len == 0u || len > 16u) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        int digit = hex_digit_value((char)data[i]);
        if (digit < 0 || value > (UINT64_MAX - (uint64_t)digit) / 16u) {
            return 0;
        }
        value = value * 16u + (uint64_t)digit;
    }
    *out = value;
    return 1;
}

static void encode_hex_le(uint64_t value, unsigned width, char *out) {
    static const char digits[] = "0123456789abcdef";
    unsigned i;
    for (i = 0u; i < width; i++) {
        out[i * 2u] = digits[(value >> (i * 8u + 4u)) & 15u];
        out[i * 2u + 1u] = digits[(value >> (i * 8u)) & 15u];
    }
    out[width * 2u] = '\0';
}

static int valid_instruction_address(const TcGdbSession *session, uint64_t address) {
    uint64_t count = (uint64_t)session->vm->debug_linemap->count;
    if (address < session->vm->debug_code_base ||
        (address - session->vm->debug_code_base) % 4u != 0u) {
        return 0;
    }
    return (address - session->vm->debug_code_base) / 4u < count;
}

static int breakpoint_index(const TcGdbSession *session, uint64_t address) {
    size_t i;
    for (i = 0u; i < session->nbreakpoints; i++) {
        if (session->breakpoints[i] == address) {
            return (int)i;
        }
    }
    return -1;
}

static int breakpoint_present(const TcGdbSession *session, uint64_t address) {
    return breakpoint_index(session, address) >= 0;
}

static void clear_breakpoints(TcGdbSession *session) {
    session->nbreakpoints = 0u;
    session->skip_breakpoint = 0;
    session->monitor_mode = TC_GDB_MONITOR_NONE;
    session->monitor_have_target = 0;
}

static uint64_t current_frame_sp(const TcGdbSession *session) {
    const TcVm *vm = session->vm;
    if (vm->debug_frame < vm->nframes && vm->frames[vm->debug_frame].valid) {
        return vm->frames[vm->debug_frame].base -
               vm->profile->region[TC_REG_FRAME].base;
    }
    return 0u;
}

static int current_frame(const TcGdbSession *session, const TcVmFrame **out) {
    const TcVm *vm = session->vm;
    if (vm->debug_frame >= vm->nframes) {
        return 0;
    }
    *out = &vm->frames[vm->debug_frame];
    return (*out)->valid;
}

static void append_register(TcGdbText *text, uint64_t value, unsigned width,
                            int unavailable) {
    char encoded[17];
    if (unavailable) {
        size_t i;
        for (i = 0u; i < width * 2u; i++) {
            if (!text_append(text, "x", 1u)) {
                return;
            }
        }
        return;
    }
    encode_hex_le(value, width, encoded);
    text_puts(text, encoded);
}

static void monitor_registers(TcGdbSession *session, TcGdbText *text) {
    const TcVm *vm = session->vm;
    int arch = arch_kind(session->profile);
    unsigned cores = core_reg_count(arch);
    unsigned pc_reg = arch_pc_reg(arch);
    unsigned base = tc_reg_base(arch);
    unsigned i;
    for (i = 0u; i < cores; i++) {
        unsigned width = core_reg_bits(arch, i) / 8u;
        if (i == pc_reg) {
            int unavailable = vm->W == 32u && arch != 0 &&
                              vm->debug_pc > UINT32_MAX;
            append_register(text, vm->debug_pc, width, unavailable);
        } else {
            append_register(text, 0u, width, 1);
        }
    }
    append_register(text, vm->debug_pc, 8u, 0);
    append_register(text, vm->debug_terminated && !vm->result->trapped
                               ? 0u
                               : (uint64_t)vm->depth,
                    8u, 0);
    append_register(text, current_frame_sp(session), 8u, 0);
    append_register(text, vm->debug_insns, 8u, 0);
    (void)base;
}

static int memory_region(const TcGdbSession *session, uint64_t address,
                         uint64_t size, TcRegionKind *out_region,
                         uint64_t *out_offset) {
    unsigned i;
    if (address >= session->vm->debug_code_base &&
        address - session->vm->debug_code_base <
            (uint64_t)session->vm->debug_linemap->count * 4u &&
        size <= (uint64_t)session->vm->debug_linemap->count * 4u -
                    (address - session->vm->debug_code_base)) {
        *out_region = TC_REGION_COUNT;
        *out_offset = address - session->vm->debug_code_base;
        return 1;
    }
    for (i = 0u; i < (unsigned)TC_REGION_COUNT; i++) {
        const TcRegionDesc *desc = &session->profile->region[i];
        if (address >= desc->base && address - desc->base <= desc->size &&
            size <= desc->size - (address - desc->base)) {
            *out_region = (TcRegionKind)i;
            *out_offset = address - desc->base;
            return 1;
        }
    }
    return 0;
}

static uint8_t *memory_host(TcGdbSession *session, TcRegionKind region,
                            uint64_t offset, uint64_t size, int *ok) {
    TcVm *vm = session->vm;
    size_t i;
    *ok = 0;
    if (region == TC_REG_FRAME) {
        for (i = vm->nframes; i > 0u; i--) {
            TcVmFrame *frame = &vm->frames[i - 1u];
            uint64_t base = frame->base - session->profile->region[TC_REG_FRAME].base;
            if (frame->valid && offset >= base && offset - base <= frame->mem_size &&
                size <= frame->mem_size - (offset - base)) {
                *ok = 1;
                return frame->mem + (offset - base);
            }
        }
        if (offset <= session->profile->region[TC_REG_FRAME].size &&
            size <= session->profile->region[TC_REG_FRAME].size - offset) {
            *ok = 1;
            return NULL;
        }
        return NULL;
    }
    if (region == TC_REG_STATIC) {
        if (offset <= vm->static_size && size <= vm->static_size - offset) {
            *ok = 1;
            return size == 0u ? vm->static_mem : vm->static_mem + offset;
        }
        return NULL;
    }
    if (region == TC_REG_DEVICE) {
        if (offset <= vm->device_size && size <= vm->device_size - offset) {
            *ok = 1;
            return size == 0u ? vm->device_mem : vm->device_mem + offset;
        }
        return NULL;
    }
    if (region == TC_REG_CODE || region == TC_REG_HEAP) {
        *ok = 1;
        return NULL;
    }
    return NULL;
}

static void mark_defined(TcGdbSession *session, TcRegionKind region,
                         uint64_t offset, uint64_t size) {
    TcVm *vm = session->vm;
    uint8_t *shadow = NULL;
    uint64_t base = 0u;
    uint64_t span = 0u;
    size_t i;
    if (!vm->profile->checked_build) {
        return;
    }
    if (region == TC_REG_STATIC) {
        shadow = vm->static_defined;
        span = vm->static_size;
    } else if (region == TC_REG_FRAME) {
        for (i = vm->nframes; i > 0u; i--) {
            TcVmFrame *frame = &vm->frames[i - 1u];
            base = frame->base - session->profile->region[TC_REG_FRAME].base;
            if (frame->valid && offset >= base && offset - base <= frame->mem_size &&
                size <= frame->mem_size - (offset - base)) {
                shadow = frame->defined;
                span = frame->defined_size;
                offset -= base;
                break;
            }
        }
    }
    if (shadow == NULL) {
        return;
    }
    for (i = 0u; i < size; i++) {
        uint64_t bit = offset + i;
        if (bit < span) {
            shadow[bit >> 3u] |= (uint8_t)(1u << (bit & 7u));
        }
    }
}

static int handle_memory_read(TcGdbSession *session, const uint8_t *data,
                              size_t len) {
    uint64_t address;
    uint64_t size;
    TcRegionKind region;
    uint64_t offset;
    uint8_t *host;
    int ok;
    int oom;
    TcGdbText text = {0};
    uint64_t i;
    if (len == 0u) {
        return send_error(session);
    }
    {
        const uint8_t *comma = (const uint8_t *)memchr(data, ',', len);
        if (comma == NULL ||
            (size_t)(comma - data) > (session->vm->W == 32u ? 8u : 16u) ||
            (size_t)(data + len - comma - 1) > 16u ||
            !payload_hex(data, (size_t)(comma - data), &address) ||
            !payload_hex(comma + 1, (size_t)(data + len - comma - 1), &size)) {
            return send_error(session);
        }
    }
    if (session->vm->W == 32u && address > UINT32_MAX) {
        return send_error(session);
    }
    if (address >= session->vm->debug_code_base) {
        uint64_t code_size = (uint64_t)session->vm->debug_linemap->count * 4u;
        uint64_t displacement = address - session->vm->debug_code_base;
        if (displacement >= code_size || size > code_size - displacement) {
            return send_error(session);
        }
        if (size > (SIZE_MAX - 1u) / 2u ||
            !text_reserve(&text, (size_t)size * 2u)) {
            oom = text.failed;
            text_dispose(&text);
            return oom ? fail_oom(session) : send_error(session);
        }
        memset(text.data, '0', (size_t)size * 2u);
        text.len = (size_t)size * 2u;
        text.data[text.len] = 0u;
        ok = send_packet(session, text.data, text.len);
        text_dispose(&text);
        return ok;
    }
    if (!memory_region(session, address, size, &region, &offset)) {
        return send_error(session);
    }
    host = memory_host(session, region, offset, size, &ok);
    if (!ok || size > (SIZE_MAX - 1u) / 2u ||
        !text_reserve(&text, (size_t)size * 2u)) {
        oom = text.failed;
        text_dispose(&text);
        return oom ? fail_oom(session) : send_error(session);
    }
    for (i = 0u; i < size; i++) {
        static const char digits[] = "0123456789abcdef";
        uint8_t byte = 0u;
        if (host != NULL) {
            byte = host[i];
        }
        text.data[text.len++] = digits[byte >> 4u];
        text.data[text.len++] = digits[byte & 15u];
    }
    if (region == TC_REG_HEAP && size != 0u) {
        uint8_t *bytes = (uint8_t *)malloc((size_t)size);
        if (bytes == NULL) {
            text_dispose(&text);
            return fail_oom(session);
        }
        memset(bytes, 0, (size_t)size);
        heap_read_overlay(session, address, bytes, (size_t)size);
        for (i = 0u; i < size; i++) {
            text.data[text.len - (size_t)size * 2u + i * 2u] =
                "0123456789abcdef"[bytes[i] >> 4u];
            text.data[text.len - (size_t)size * 2u + i * 2u + 1u] =
                "0123456789abcdef"[bytes[i] & 15u];
        }
        free(bytes);
    }
    text.data[text.len] = 0u;
    ok = send_packet(session, text.data, text.len);
    text_dispose(&text);
    return ok;
}

static int handle_memory_write(TcGdbSession *session, const uint8_t *data,
                               size_t len) {
    const uint8_t *comma;
    const uint8_t *colon;
    uint64_t address;
    uint64_t size;
    TcRegionKind region;
    uint64_t offset;
    uint8_t *host;
    uint8_t *bytes = NULL;
    size_t bytes_len = 0u;
    size_t hex_len;
    int ok;
    if (!session->options->allow_write) {
        return send_error(session);
    }
    comma = (const uint8_t *)memchr(data, ',', len);
    if (comma == NULL) {
        return send_error(session);
    }
    colon = (const uint8_t *)memchr(comma + 1, ':', (size_t)(data + len - comma - 1));
    if (colon == NULL ||
        (size_t)(comma - data) > (session->vm->W == 32u ? 8u : 16u) ||
        (size_t)(colon - comma - 1) > 16u ||
        !payload_hex(data, (size_t)(comma - data), &address) ||
        !payload_hex(comma + 1, (size_t)(colon - comma - 1), &size) ||
        (session->vm->W == 32u && address > UINT32_MAX)) {
        return send_error(session);
    }
    hex_len = (size_t)(data + len - colon - 1);
    if (size > SIZE_MAX / 2u || hex_len != size * 2u) {
        return send_error(session);
    }
    if (size != 0u) {
        bytes = (uint8_t *)malloc((size_t)size);
        if (bytes == NULL) {
            return fail_oom(session);
        }
        if (tc_gdb_rsp_hex_decode((const char *)colon + 1, hex_len, bytes,
                                  (size_t)size, &bytes_len) != 0) {
            free(bytes);
            return send_error(session);
        }
    }
    if (!memory_region(session, address, size, &region, &offset) ||
        (region != TC_REG_FRAME && region != TC_REG_STATIC && region != TC_REG_HEAP)) {
        free(bytes);
        return send_error(session);
    }
    if (region == TC_REG_HEAP) {
        ok = heap_write(session, address, bytes, (size_t)size);
    } else {
        host = memory_host(session, region, offset, size, &ok);
        if (ok) {
            if (host != NULL && size != 0u) {
                memcpy(host, bytes, (size_t)size);
            }
            mark_defined(session, region, offset, size);
        }
    }
    free(bytes);
    if (!ok && region == TC_REG_HEAP) {
        return fail_oom(session);
    }
    return ok ? send_ok(session) : send_error(session);
}

static int handle_register_read(TcGdbSession *session, const uint8_t *data,
                                size_t len) {
    uint64_t regnum = 0u;
    int arch = arch_kind(session->profile);
    unsigned total = core_reg_count(arch) + 4u;
    if (!payload_hex(data, len, &regnum) || regnum >= total) {
        return send_error(session);
    }
    if (session->vm->W == 32u && arch != 0 && regnum == arch_pc_reg(arch) &&
        session->vm->debug_pc > UINT32_MAX) {
        return send_text(session, "xxxxxxxx");
    }
    if (regnum >= core_reg_count(arch)) {
        uint64_t value = 0u;
        char encoded[17];
        switch (regnum - core_reg_count(arch)) {
        case 0u: value = session->vm->debug_pc; break;
        case 1u:
            value = session->vm->debug_terminated && !session->vm->result->trapped
                        ? 0u
                        : (uint64_t)session->vm->depth;
            break;
        case 2u: value = current_frame_sp(session); break;
        default: value = session->vm->debug_insns; break;
        }
        encode_hex_le(value, 8u, encoded);
        return send_text(session, encoded);
    }
    if (regnum == arch_pc_reg(arch)) {
        char encoded[17];
        encode_hex_le(session->vm->debug_pc,
                      core_reg_bits(arch, (unsigned)regnum) / 8u, encoded);
        return send_text(session, encoded);
    }
    {
        char unavailable[17];
        unsigned width = core_reg_bits(arch, (unsigned)regnum) / 8u;
        memset(unavailable, 'x', width * 2u);
        unavailable[width * 2u] = '\0';
        return send_text(session, unavailable);
    }
}

static int handle_register_write(TcGdbSession *session, const uint8_t *data,
                                 size_t len) {
    const uint8_t *equals;
    uint64_t regnum;
    uint64_t value;
    int arch = arch_kind(session->profile);
    unsigned pc_reg = arch_pc_reg(arch);
    unsigned width = core_reg_bits(arch, pc_reg) / 8u;
    const TcLineMapEntry *entry;
    TcVmFrame *frame;
    unsigned i;
    if (!session->options->allow_write || session->state == TC_GDB_TERMINATED) {
        return send_error(session);
    }
    equals = (const uint8_t *)memchr(data, '=', len);
    value = 0u;
    if (equals == NULL || !payload_hex(data, (size_t)(equals - data), &regnum) ||
        regnum != pc_reg || (size_t)(data + len - equals - 1) != width * 2u) {
        return send_error(session);
    }
    for (i = 0u; i < width; i++) {
        int high = hex_digit_value((char)equals[1u + i * 2u]);
        int low = hex_digit_value((char)equals[2u + i * 2u]);
        if (high < 0 || low < 0) {
            return send_error(session);
        }
        value |= (uint64_t)((unsigned)high * 16u + (unsigned)low) << (i * 8u);
    }
    if (!valid_instruction_address(session, value)) {
        return send_error(session);
    }
    if (session->vm->debug_frame >= session->vm->nframes) {
        return send_error(session);
    }
    entry = &session->vm->debug_linemap->entries[
        (value - session->vm->debug_code_base) / 4u];
    frame = &session->vm->frames[session->vm->debug_frame];
    if (entry->bb >= frame->unit->nblocks ||
        entry->insn >= frame->unit->blocks[entry->bb].n ||
        &frame->unit->blocks[entry->bb].instrs[entry->insn] != entry->instr) {
        return send_error(session);
    }
    frame->resume_block = entry->bb;
    frame->resume_pc = entry->insn;
    frame->has_resume = 1;
    frame->debug_counted = 0;
    session->vm->debug_pc = value;
    return send_ok(session);
}

static int handle_q_xfer(TcGdbSession *session, const uint8_t *data, size_t len) {
    char *xml = NULL;
    const char *annex = NULL;
    const uint8_t *last_separator = NULL;
    const uint8_t *middle_colon = NULL;
    uint64_t offset;
    uint64_t length;
    size_t xml_len;
    size_t take;
    char prefix;
    TcGdbText response = {0};
    int ok;
    for (size_t i = 0u; i < len; i++) {
        if (data[i] == (uint8_t)':' || data[i] == (uint8_t)',') {
            last_separator = &data[i];
        }
    }
    if (last_separator == NULL) {
        return send_error(session);
    }
    for (size_t i = 0u; i < (size_t)(last_separator - data); i++) {
        if (data[i] == (uint8_t)':') {
            middle_colon = &data[i];
        }
    }
    if (middle_colon == NULL ||
        !payload_hex(middle_colon + 1,
                     (size_t)(last_separator - middle_colon - 1), &offset) ||
        !payload_hex(last_separator + 1,
                     (size_t)(data + len - last_separator - 1), &length)) {
        return send_error(session);
    }
    if (len > 31u && memcmp(data, "qXfer:features:read:target.xml:", 31u) == 0) {
        size_t cap = 16384u;
        xml = (char *)malloc(cap);
        if (xml == NULL || tc_gdb_rsp_target_xml(session->profile, xml, cap) != 0) {
            free(xml);
            return fail_oom(session);
        }
        annex = "target.xml";
    } else if (len > 22u && memcmp(data, "qXfer:memory-map:read:", 22u) == 0) {
        size_t cap = 4096u;
        xml = (char *)malloc(cap);
        if (xml == NULL || tc_gdb_rsp_memory_map_xml(session->profile, xml, cap) != 0) {
            free(xml);
            return fail_oom(session);
        }
        annex = "memory-map";
    }
    (void)annex;
    if (xml == NULL) {
        return send_text(session, "");
    }
    xml_len = strlen(xml);
    if (offset > xml_len) {
        free(xml);
        return send_error(session);
    }
    if (length > TC_GDB_RSP_PACKET_SIZE - 1u) {
        length = TC_GDB_RSP_PACKET_SIZE - 1u;
    }
    take = xml_len - (size_t)offset;
    if (take > length) {
        take = (size_t)length;
    }
    prefix = (size_t)offset + take < xml_len ? 'm' : 'l';
    if ((take == 0u && prefix == 'm') || !text_append(&response, &prefix, 1u) ||
        !text_append(&response, xml + offset, take)) {
        text_dispose(&response);
        free(xml);
        return fail_oom(session);
    }
    ok = send_packet(session, response.data, response.len);
    text_dispose(&response);
    free(xml);
    return ok;
}

static const char *entity_module(const TcGdbSession *session,
                                 const TcVmFrame *frame) {
    if (frame->func_id == TC_UNIT_TOP_LEVEL) {
        size_t bi;
        if (frame->unit != NULL) {
            for (bi = 0u; bi < frame->unit->nblocks; bi++) {
                if (frame->unit->blocks[bi].n != 0u) {
                    return frame->unit->blocks[bi].instrs[0].span.file != NULL
                               ? frame->unit->blocks[bi].instrs[0].span.file
                               : "-";
                }
            }
        }
        return "-";
    }
    return session->vm->ir->modules[
        session->vm->ir->funcs[frame->func_id].module].path;
}

static int location_for_frame(TcGdbSession *session, const TcVmFrame *frame,
                              const TcInstr **out_instr, uint32_t *out_bb,
                              uint32_t *out_insn) {
    if (session->vm->result->trapped && frame == &session->vm->frames[session->vm->debug_frame] &&
        session->vm->current_instr != NULL) {
        *out_instr = session->vm->current_instr;
        *out_bb = session->vm->current_bb;
        *out_insn = session->vm->current_insn;
        return 1;
    }
    if (frame->has_resume && frame->resume_block < frame->unit->nblocks) {
        const TcBlock *block = &frame->unit->blocks[frame->resume_block];
        if (frame->resume_pc < block->n) {
            *out_instr = &block->instrs[frame->resume_pc];
            *out_bb = frame->resume_block;
            *out_insn = (uint32_t)frame->resume_pc;
            return 1;
        }
    }
    if (frame->unit->entry < frame->unit->nblocks &&
        frame->unit->blocks[frame->unit->entry].n != 0u) {
        *out_instr = &frame->unit->blocks[frame->unit->entry].instrs[0];
        *out_bb = frame->unit->entry;
        *out_insn = 0u;
        return 1;
    }
    return 0;
}

static int handle_regs_packet(TcGdbSession *session) {
    TcGdbText text = {0};
    monitor_registers(session, &text);
    if (text.failed) {
        text_dispose(&text);
        return fail_oom(session);
    }
    {
        int ok = send_packet(session, text.data, text.len);
        text_dispose(&text);
        return ok;
    }
}

static int monitor_print_ir(TcGdbSession *session, const TcInstr *instr,
                            TcGdbText *out) {
    TcIR view;
    TcUnit unit;
    TcBlock block;
    char *printed = NULL;
    size_t printed_len = 0u;
    FILE *stream = open_memstream(&printed, &printed_len);
    int found = 0;
    if (stream == NULL) {
        return 0;
    }
    memset(&view, 0, sizeof(view));
    memset(&unit, 0, sizeof(unit));
    memset(&block, 0, sizeof(block));
    block.instrs = (TcInstr *)instr;
    block.n = 1u;
    unit.blocks = &block;
    unit.nblocks = 1u;
    unit.func_id = TC_UNIT_TOP_LEVEL;
    view = *session->vm->ir;
    view.nfuncs = 0u;
    view.nconsts = 0u;
    view.nobjects = 0u;
    view.nstructs = 0u;
    view.units = &unit;
    view.nunits = 1u;
    if (tc_ir_print(&view, stream) == 0 && fflush(stream) == 0) {
        char *line = printed;
        while (line != NULL && *line != '\0') {
            char *end = strchr(line, '\n');
            size_t line_len = end != NULL ? (size_t)(end - line) + 1u
                                          : strlen(line);
            if (line_len > 6u && memcmp(line, "      ", 6u) == 0) {
                text_append(out, line + 6u, line_len - 6u);
                found = 1;
                break;
            }
            line = end != NULL ? end + 1 : NULL;
        }
    }
    fclose(stream);
    free(printed);
    return found && !out->failed;
}

static int vreg_definition_dominates(const TcVmFrame *frame, uint32_t vreg,
                                     uint32_t current_block,
                                     size_t current_pc, const TcVmObject *object) {
    const TcUnit *unit = frame->unit;
    size_t bi;
    size_t ii;
    int found = 0;
    if (object != NULL && !object->writable) {
        return 1;
    }
    for (bi = 0u; bi < unit->nblocks; bi++) {
        const TcBlock *block = &unit->blocks[bi];
        for (ii = 0u; ii < block->n; ii++) {
            const TcInstr *instr = &block->instrs[ii];
            uint32_t base = instr->dst.kind == TC_OP_VREG
                                ? instr->dst.vreg
                                : instr->dst.kind == TC_OP_FIELD
                                      ? instr->dst.field.base
                                      : UINT32_MAX;
            if (base != vreg) {
                continue;
            }
            found = 1;
            if (bi == current_block) {
                if (ii < current_pc) {
                    return 1;
                }
            } else {
                /* The definition dominates precisely when the current block
                 * cannot be reached without passing through its block. */
                uint8_t *seen = (uint8_t *)calloc(unit->nblocks, 1u);
                uint32_t *stack = (uint32_t *)malloc(unit->nblocks * sizeof(uint32_t));
                size_t top = 0u;
                int bypass = 0;
                if (seen == NULL || stack == NULL) {
                    free(seen);
                    free(stack);
                    return -1;
                }
                if (unit->entry == bi) {
                    free(seen);
                    free(stack);
                    return 1;
                }
                seen[unit->entry] = 1u;
                stack[top++] = unit->entry;
                while (top != 0u && !bypass) {
                    const TcBlock *at = &unit->blocks[stack[--top]];
                    size_t s;
                    for (s = 0u; s < at->nsucc; s++) {
                        uint32_t next = at->succs[s];
                        if (next >= unit->nblocks || next == bi) {
                            continue;
                        }
                        if (next == current_block) {
                            bypass = 1;
                            break;
                        }
                        if (!seen[next]) {
                            seen[next] = 1u;
                            stack[top++] = next;
                        }
                    }
                }
                free(seen);
                free(stack);
                if (!bypass) {
                    return 1;
                }
            }
        }
    }
    (void)found;
    return 0;
}

static int monitor_vreg(TcGdbSession *session, const char *args, TcGdbText *out) {
    const TcVmFrame *frame;
    const char *raw = args;
    const char *colon;
    uint64_t number;
    size_t raw_len;
    size_t digits_len;
    const TcType *type;
    const TcVmObject *object = NULL;
    uint32_t vreg;
    if (session->state == TC_GDB_TERMINATED && !session->vm->result->trapped) {
        return 0;
    }
    if (!current_frame(session, &frame)) {
        return 0;
    }
    raw_len = strlen(raw);
    colon = strrchr(raw, ':');
    digits_len = colon != NULL ? strlen(colon + 1) : raw_len;
    if (!parse_decimal(colon != NULL ? colon + 1 : raw, digits_len, &number) ||
        number >= frame->unit->nvregs) {
        return 0;
    }
    vreg = (uint32_t)number;
    if (colon != NULL) {
        const char *selector = raw;
        size_t selector_len = (size_t)(colon - raw);
        if (frame->func_id == TC_UNIT_TOP_LEVEL) {
            if (selector_len != 5u || memcmp(selector, "<top>", 5u) != 0) {
                return 0;
            }
        } else {
            const char *split = strrchr(selector, ':');
            const TcFunc *func = &session->vm->ir->funcs[frame->func_id];
            const char *module = session->vm->ir->modules[func->module].path;
            if (split == NULL) {
                return 0;
            }
            {
                size_t module_len = (size_t)(split - selector);
                size_t func_len = selector_len - module_len - 1u;
                if (module_len != strlen(module) ||
                    memcmp(selector, module, module_len) != 0 ||
                    func_len != strlen(func->name) ||
                    memcmp(split + 1, func->name, func_len) != 0) {
                    return 0;
                }
            }
        }
    }
    type = frame->unit->vreg_types[vreg];
    if (type == NULL || type->tag == TC_TY_STRUCT || type->tag == TC_TY_VOID) {
        return 0;
    }
    if (frame->vreg_obj[vreg] != UINT32_MAX &&
        frame->vreg_obj[vreg] < session->vm->nobjects) {
        object = &session->vm->objects[frame->vreg_obj[vreg]];
    }
    {
        const TcInstr *instr;
        uint32_t bb;
        uint32_t insn;
        int dominates;
        if (!location_for_frame(session, frame, &instr, &bb, &insn)) {
            return 0;
        }
        dominates = vreg_definition_dominates(frame, vreg, bb, insn, object);
        if (dominates < 0) {
            fail_oom(session);
            return 0;
        }
        if (!dominates) {
            return 0;
        }
    }
    if (type->tag == TC_TY_PTR || type->tag == TC_TY_FNPTR) {
        uint64_t value = frame->regs[vreg].bits;
        if (value == 0u) {
            text_puts(out, "null\n");
            return 1;
        }
        {
            unsigned i;
            int found = 0;
            for (i = 0u; i < (unsigned)TC_REGION_COUNT; i++) {
                const TcRegionDesc *desc = &session->profile->region[i];
                if (value >= desc->base && value - desc->base < desc->size) {
                    text_printf(out, "%s+0x%" PRIx64 "\n", region_name((TcRegionKind)i),
                                value - desc->base);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                text_printf(out, "0x%0*" PRIx64 "\n", session->vm->W == 32u ? 8 : 16,
                            value);
            }
        }
        return !out->failed;
    }
    {
        unsigned bits = tc_type_bits(type, session->vm->W);
        if (bits == 0u || bits > 64u) {
            return 0;
        }
        text_printf(out, "0x%0*" PRIx64 "\n", (int)(bits / 4u),
                    frame->regs[vreg].bits & tc_sem_mask(bits));
    }
    return !out->failed;
}

static int monitor_ir_command(TcGdbSession *session, const char *args,
                              TcGdbText *out) {
    uint64_t address;
    if (*args == '\0') {
        if (session->state == TC_GDB_TERMINATED ||
            !valid_instruction_address(session, session->vm->debug_pc)) {
            return 0;
        }
        address = session->vm->debug_pc;
    } else {
        size_t len;
        if (args[0] != '0' || args[1] != 'x') {
            return 0;
        }
        len = strlen(args + 2);
        if (len == 0u || len > 16u) {
            return 0;
        }
        if (!parse_hex(args + 2, len, &address) ||
            (session->vm->W == 32u && address > UINT32_MAX) ||
            !valid_instruction_address(session, address)) {
            return 0;
        }
    }
    return monitor_print_ir(session,
        session->vm->debug_linemap->entries[
            (address - session->vm->debug_code_base) / 4u].instr, out);
}

static int monitor_frames(TcGdbSession *session, TcGdbText *out) {
    size_t i;
    for (i = 0u; i < session->vm->nframes; i++) {
        const TcVmFrame *frame = &session->vm->frames[i];
        const TcInstr *instr;
        uint32_t bb;
        uint32_t insn;
        size_t index = 0u;
        if (!frame->valid) {
            continue;
        }
        if (!location_for_frame(session, frame, &instr, &bb, &insn) ||
            !tc_linemap_instr_index(session->vm->debug_linemap, instr, &index)) {
            return 0;
        }
        if (frame->func_id == TC_UNIT_TOP_LEVEL) {
            text_printf(out,
                "depth=%zu kind=unit module=%s func=<top> insn=%zu @%s:%u:%u\n",
                i, entity_module(session, frame), index, instr->span.file,
                instr->span.line, instr->span.column);
        } else {
            const TcFunc *func = &session->vm->ir->funcs[frame->func_id];
            text_printf(out,
                "depth=%zu kind=function module=%s func=%s insn=%zu @%s:%u:%u\n",
                i, session->vm->ir->modules[func->module].path, func->name,
                index, instr->span.file, instr->span.line, instr->span.column);
        }
    }
    return !out->failed;
}

static int monitor_objects(TcGdbSession *session, TcGdbText *out) {
    size_t i;
    for (i = 0u; i < session->vm->nobjects; i++) {
        const TcVmObject *object = &session->vm->objects[i];
        const char *lifetime = "?";
        if (object->ir_object < session->vm->ir->nobjects) {
            const TcObject *source = &session->vm->ir->objects[object->ir_object];
            lifetime = source->lifetime == TC_LIFE_FRAME ? "frame"
                       : source->lifetime == TC_LIFE_PROGRAM ? "program"
                                                              : "host";
        }
        text_printf(out,
            "id=o%u region=%s offset=0x%" PRIx64 " size=%" PRIu64
            " lifetime=%s frame=%u\n",
            object->ir_object == UINT32_MAX ? (unsigned)i : object->ir_object,
            region_name(object->region),
            object->start - session->profile->region[object->region].base,
            object->size, lifetime,
            object->frame == UINT32_MAX ? 0u : object->frame);
    }
    return !out->failed;
}

static int monitor_funcs(TcGdbSession *session, TcGdbText *out) {
    size_t i;
    for (i = 0u; i < session->vm->ir->nfuncs; i++) {
        const TcFunc *func = &session->vm->ir->funcs[i];
        const TcUnit *unit = func->body;
        size_t first = SIZE_MAX;
        size_t last = 0u;
        size_t bi;
        if (unit == NULL) {
            continue;
        }
        for (bi = 0u; bi < unit->nblocks; bi++) {
            size_t ii;
            for (ii = 0u; ii < unit->blocks[bi].n; ii++) {
                size_t index;
                if (!tc_linemap_instr_index(session->vm->debug_linemap,
                                            &unit->blocks[bi].instrs[ii], &index)) {
                    return 0;
                }
                if (first == SIZE_MAX || index < first) {
                    first = index;
                }
                if (index > last) {
                    last = index;
                }
            }
        }
        if (first != SIZE_MAX) {
            uint64_t funcaddr = tc_vm_func_address(session->vm, (uint32_t)i);
            text_printf(out,
                "module=%s func=%s funcaddr=%s+0x%" PRIx64
                " insns=[%zu,%zu] addr=[0x%" PRIx64 ",0x%" PRIx64 "]\n",
                session->vm->ir->modules[func->module].path, func->name,
                region_name(TC_REG_CODE),
                funcaddr - session->profile->region[TC_REG_CODE].base,
                first, last, session->vm->debug_code_base + first * 4u,
                session->vm->debug_code_base + last * 4u);
        }
    }
    return !out->failed;
}

static int monitor_regions(TcGdbSession *session, TcGdbText *out) {
    static const TcRegionKind order[TC_REGION_COUNT] = {
        TC_REG_FRAME, TC_REG_STATIC, TC_REG_DEVICE, TC_REG_HEAP, TC_REG_CODE
    };
    size_t i;
    for (i = 0u; i < (unsigned)TC_REGION_COUNT; i++) {
        TcRegionKind region = order[i];
        uint64_t used = 0u;
        if (region == TC_REG_FRAME) {
            used = session->vm->frame_top;
        } else if (region == TC_REG_STATIC) {
            used = session->vm->static_size;
        }
        text_printf(out, "%s base=0x%" PRIx64 " size=0x%" PRIx64 " used=%" PRIu64 "\n",
                    region_name(region), session->profile->region[region].base,
                    session->profile->region[region].size, used);
    }
    return !out->failed;
}

static int monitor_stats(TcGdbSession *session, TcGdbText *out) {
    typedef struct {
        char name[64];
        uint64_t count;
    } TcGdbOpCount;
    TcVmStats *stats = session->vm->stats;
    TcGdbOpCount *counts;
    size_t count = 0u;
    size_t i;
    if (stats == NULL) {
        return 0;
    }
    text_printf(out,
        "executed=%" PRIu64 "\ndepth_max=%" PRIu64 "\n"
        "region_peak.frame=%" PRIu64 "\nregion_peak.static=%" PRIu64 "\n"
        "region_peak.device=%" PRIu64 "\nregion_peak.heap=%" PRIu64 "\n"
        "region_peak.code=%" PRIu64 "\nallocations=%" PRIu64 "\nio_bytes=%" PRIu64 "\n",
        stats->executed, stats->depth_max, stats->region_peak[TC_REG_FRAME],
        stats->region_peak[TC_REG_STATIC], stats->region_peak[TC_REG_DEVICE],
        stats->region_peak[TC_REG_HEAP], stats->region_peak[TC_REG_CODE],
        stats->allocations, stats->io_bytes);
    if (stats->trapped) {
        const TcDiagInfo *info = tc_diag_info((TcDiagCode)stats->first_trap);
        text_printf(out, "first_trap=%s\n", info != NULL ? info->symbol : "TC_RE_TRAP");
    }
    counts = (TcGdbOpCount *)calloc((size_t)TC_VM_OP_SLOT_COUNT,
                                   sizeof(*counts));
    if (counts == NULL) {
        return fail_oom(session);
    }
    for (i = 0u; i < (size_t)TC_VM_OP_SLOT_COUNT; i++) {
        if (stats->op_counts[i] != 0u) {
            TcInstr instr;
            memset(&instr, 0, sizeof(instr));
            instr.op = (TcOpcode)(i / 6u);
            instr.mode = (TcOpMode)(i % 6u);
            (void)tc_ir_instr_name(&instr, counts[count].name,
                                   sizeof(counts[count].name));
            counts[count].count = stats->op_counts[i];
            count++;
        }
    }
    /* Sort the small fixed table directly by name. */
    for (i = 1u; i < count; i++) {
        TcGdbOpCount value = counts[i];
        size_t j = i;
        while (j != 0u && strcmp(counts[j - 1u].name, value.name) > 0) {
            counts[j] = counts[j - 1u];
            j--;
        }
        counts[j] = value;
    }
    for (i = 0u; i < count; i++) {
        text_printf(out, "op.%s=%" PRIu64 "\n", counts[i].name, counts[i].count);
    }
    free(counts);
    return !out->failed;
}

static int monitor_trap(TcGdbSession *session, TcGdbText *out) {
    const TcVmResult *result = session->vm->result;
    const TcDiagInfo *info;
    char op[64];
    if (!result->trapped) {
        return 0;
    }
    info = tc_diag_info((TcDiagCode)result->kind);
    tc_ir_instr_name(&(TcInstr){.op = result->op}, op, sizeof(op));
    text_printf(out,
        "code=%s span=%s:%u:%u kind=trap module=%s func=%s bb=%u insn=%u op=%s depth=%u\n",
        info != NULL ? info->symbol : "TC_RE_IO",
        result->span.file != NULL ? result->span.file : "-",
        result->span.line, result->span.column,
        result->module != NULL ? result->module : "-",
        result->func != NULL ? result->func : "-",
        result->bb, result->insn, op, result->depth);
    return !out->failed;
}

static int send_monitor_text(TcGdbSession *session, const TcGdbText *text) {
    size_t pos = 0u;
    while (pos < text->len) {
        size_t take = text->len - pos;
        TcGdbText packet = {0};
        int ok;
        if (take > (TC_GDB_RSP_PACKET_SIZE - 1u) / 2u) {
            take = (TC_GDB_RSP_PACKET_SIZE - 1u) / 2u;
        }
        if (!text_append(&packet, "O", 1u)) {
            text_dispose(&packet);
            return fail_oom(session);
        }
        {
            size_t i;
            static const char digits[] = "0123456789abcdef";
            if (!text_reserve(&packet, take * 2u)) {
                text_dispose(&packet);
                return fail_oom(session);
            }
            for (i = 0u; i < take; i++) {
                packet.data[packet.len++] = digits[text->data[pos + i] >> 4u];
                packet.data[packet.len++] = digits[text->data[pos + i] & 15u];
            }
            packet.data[packet.len] = 0u;
        }
        ok = send_packet(session, packet.data, packet.len);
        text_dispose(&packet);
        if (!ok) {
            return 0;
        }
        pos += take;
    }
    return 1;
}

static unsigned debug_boundary(void *ctx, TcVm *vm, uint32_t frame_index,
                               uint64_t address);

static int preserve_input_token(TcGdbSession *session) {
    TcInput *input = &session->vm->input;
    size_t remaining = input->replay != NULL
                           ? input->replay_len - input->replay_pos
                           : 0u;
    size_t buffered = input->len - input->pos;
    size_t combined;
    size_t offset;
    char *replay;
    if (input->token_len > SIZE_MAX - remaining ||
        buffered > SIZE_MAX - input->token_len - remaining) {
        return 0;
    }
    combined = input->token_len + remaining + buffered;
    if (combined == 0u) {
        return 1;
    }
    replay = (char *)input->alloc->alloc(input->alloc->ctx, combined);
    if (replay == NULL) {
        return 0;
    }
    offset = input->token_len;
    if (input->token_len != 0u) {
        memcpy(replay, input->token, input->token_len);
    }
    if (remaining != 0u) {
        memcpy(replay + offset, input->replay + input->replay_pos, remaining);
        offset += remaining;
    }
    if (buffered != 0u) {
        memcpy(replay + offset, input->buffer + input->pos, buffered);
    }
    if (input->replay != NULL) {
        input->alloc->free(input->alloc->ctx, input->replay);
    }
    input->replay = replay;
    input->replay_cap = combined;
    input->replay_len = combined;
    input->replay_pos = 0u;
    input->pos = 0u;
    input->len = 0u;
    /* Replayed bytes were already counted before the debugger paused. */
    input->consumed -= (uint64_t)input->token_len;
    input->token_len = 0u;
    input->token[0] = '\0';
    input->token_committed = 0;
    return 1;
}

static int run_instruction(TcGdbSession *session, int single) {
    TcVmFlow flow;
    uint32_t frame_index = session->vm->debug_frame;
    session->state = TC_GDB_RUNNING;
    session->stop_reason_interrupt = 0;
    session->suppress_trap_reply = 0;
    flow = tc_vm_execute_resume(session->vm, frame_index, single);
    if (session->input_paused) {
        session->input_paused = 0;
        if (!preserve_input_token(session)) {
            tc_vm_impl(session->vm, TC_IMPL_OOM);
            session->state = TC_GDB_TERMINATED;
            session->exiting = 1;
            return -1;
        }
    }
    if (flow == TC_VM_FLOW_FAIL) {
        session->state = TC_GDB_TERMINATED;
        session->vm->debug_terminated = 1;
        if (session->vm->result->trapped) {
            session->vm->debug_pc = tc_vm_debug_pc(session->vm);
            session->vm->debug_frame = session->vm->nframes - 1u;
        }
        if (session->vm->stats != NULL) {
            session->vm->stats->trapped = session->vm->result->trapped;
            session->vm->stats->first_trap = session->vm->result->kind;
        }
        tc_vm_trace_footer(session->vm,
                           session->vm->result->trapped ? "trap" : "error");
        if (!session->vm->result->trapped) {
            if (session->vm->impl == TC_IMPL_OK) {
                tc_vm_impl(session->vm, TC_IMPL_INTERNAL);
            }
            session->exiting = 1;
            return -1;
        }
        return 2;
    }
    if (flow == TC_VM_FLOW_END || flow == TC_VM_FLOW_RETURN) {
        session->state = TC_GDB_TERMINATED;
        session->vm->debug_terminated = 1;
        session->vm->debug_pc = session->vm->debug_code_base +
                                (uint64_t)session->vm->debug_linemap->count * 4u;
        session->vm->debug_frame = UINT32_MAX;
        if (session->vm->stats != NULL) {
            session->vm->stats->trapped = 0;
        }
        tc_vm_trace_footer(session->vm, "ok");
        return 1;
    }
    session->state = TC_GDB_STOPPED;
    return 0;
}

static int continue_execution(TcGdbSession *session, int single, int step_over) {
    int result;
    if (session->detached || session->exiting) {
        return 0;
    }
    if (session->state == TC_GDB_TERMINATED) {
        return session->vm->result->trapped ? 2 : 1;
    }
    if (step_over && session->vm->debug_insns != 0u &&
        breakpoint_present(session, session->vm->debug_pc)) {
        session->skip_breakpoint = 1;
        session->skip_address = session->vm->debug_pc;
    }
    result = run_instruction(session, single);
    if (result != 0 || single) {
        return result;
    }
    if (!session->detached && !session->exiting &&
        session->state != TC_GDB_TERMINATED && session->vm->debug_stop == 0u) {
        return -1;
    }
    return 0;
}

static int send_terminated_reply(TcGdbSession *session) {
    char reply[32];
    snprintf(reply, sizeof(reply), "W%02x",
             (unsigned)(session->vm->result->trapped
                            ? session->profile->exit.trap_status
                            : session->profile->exit.normal_status) & 255u);
    return send_text(session, reply);
}

static int send_stop_reply(TcGdbSession *session, int execution_result) {
    char reply[64];
    if (!session->connected || session->detached) {
        return 1;
    }
    if (execution_result < 0) {
        return 1;
    }
    if (execution_result == 2) {
        return send_text(session, "T0bthread:1;");
    }
    if (execution_result == 1 || session->state == TC_GDB_TERMINATED) {
        snprintf(reply, sizeof(reply), "W%02x",
                 (unsigned)(session->vm->result->trapped
                                ? session->profile->exit.trap_status
                                : session->profile->exit.normal_status) & 255u);
        return send_text(session, reply);
    }
    if (session->stop_reason_interrupt) {
        return send_text(session, "T02thread:1;");
    }
    return send_text(session, "T05thread:1;");
}

static int handle_execute(TcGdbSession *session, int single) {
    int result;
    session->monitor_mode = TC_GDB_MONITOR_NONE;
    session->monitor_have_target = 0;
    result = continue_execution(session, single, !single);
    return send_stop_reply(session, result);
}

static int handle_breakpoint(TcGdbSession *session, int insert,
                             const uint8_t *data, size_t len) {
    uint64_t address;
    int index;
    const uint8_t *comma = (const uint8_t *)memchr(data, ',', len);
    if (comma != NULL) {
        if ((size_t)(data + len - comma - 1) != 1u || comma[1] != (uint8_t)'4') {
            return send_error(session);
        }
        len = (size_t)(comma - data);
    }
    if (len > (session->vm->W == 32u ? 8u : 16u) ||
        !payload_hex(data, len, &address) || !valid_instruction_address(session, address)) {
        return send_error(session);
    }
    index = breakpoint_index(session, address);
    if (insert) {
        if (index >= 0) {
            return send_ok(session);
        }
        if (session->nbreakpoints >= 64u) {
            return send_error(session);
        }
        session->breakpoints[session->nbreakpoints++] = address;
    } else if (index >= 0) {
        session->breakpoints[index] = session->breakpoints[--session->nbreakpoints];
    }
    return send_ok(session);
}

static int handle_thread_packet(TcGdbSession *session, const uint8_t *data,
                                size_t len) {
    if (memcmp(data, "H", 1u) == 0 && len > 1u) {
        if ((len == 4u && memcmp(data, "Hc-1", 4u) == 0) ||
            (len == 3u && (memcmp(data, "Hg0", 3u) == 0 ||
                           memcmp(data, "Hc1", 3u) == 0 ||
                           memcmp(data, "Hg1", 3u) == 0))) {
            return send_ok(session);
        }
        return send_error(session);
    }
    if (memcmp(data, "qC", 2u) == 0 && len == 2u) {
        return send_text(session, "QC1");
    }
    if (memcmp(data, "qfThreadInfo", 12u) == 0 && len == 12u) {
        if (!session->thread_listed) {
            session->thread_listed = 1;
            return send_text(session, "m1");
        }
        return send_text(session, "l");
    }
    if ((memcmp(data, "qsThreadInfo", 12u) == 0 && len == 12u) ||
        (memcmp(data, "qAttached", 9u) == 0 && len == 9u)) {
        return send_text(session, memcmp(data, "qAttached", 9u) == 0 ? "0" : "l");
    }
    return send_text(session, "");
}

static int handle_vcont(TcGdbSession *session, const uint8_t *data, size_t len) {
    int single;
    if (len == 6u && memcmp(data, "vCont?", 6u) == 0) {
        return send_text(session, "vCont;s;c");
    }
    if (len < 7u || memcmp(data, "vCont;", 6u) != 0) {
        return send_text(session, "");
    }
    if (data[6] == 's') {
        single = 1;
    } else if (data[6] == 'c') {
        single = 0;
    } else {
        return send_text(session, "");
    }
    if (len == 9u && data[7] == ':' &&
        (data[8] == 's' || data[8] == 'c')) {
        return send_text(session, "");
    }
    if (len == 9u && data[7] == ':' &&
        hex_digit_value((char)data[8]) >= 0) {
        if (data[8] != '1') {
            return send_error(session);
        }
    } else if (len != 7u) {
        return send_text(session, "");
    }
    return handle_execute(session, single);
}

static int handle_monitor(TcGdbSession *session, const uint8_t *data, size_t len) {
    uint8_t *decoded = NULL;
    size_t decoded_len = 0u;
    char *command = NULL;
    TcGdbText output = {0};
    int ok = 0;
    int result = 0;
    if (len < 6u || memcmp(data, "qRcmd,", 6u) != 0) {
        return send_error(session);
    }
    decoded = (uint8_t *)malloc(len / 2u + 1u);
    if (decoded == NULL) {
        return fail_oom(session);
    }
    if (tc_gdb_rsp_hex_decode((const char *)data + 6, len - 6u, decoded,
                              len / 2u, &decoded_len) != 0) {
        free(decoded);
        return send_error(session);
    }
    decoded[decoded_len] = 0u;
    command = (char *)decoded;
    if (strncmp(command, "monitor ", 8u) == 0) {
        command += 8;
    }
    if (strcmp(command, "regions") == 0) {
        ok = monitor_regions(session, &output);
    } else if (strcmp(command, "funcs") == 0) {
        ok = monitor_funcs(session, &output);
    } else if (strcmp(command, "objects") == 0) {
        ok = monitor_objects(session, &output);
    } else if (strcmp(command, "frames") == 0) {
        ok = monitor_frames(session, &output);
    } else if (strcmp(command, "stats") == 0) {
        ok = monitor_stats(session, &output);
    } else if (strcmp(command, "trap") == 0) {
        ok = monitor_trap(session, &output);
    } else if (strcmp(command, "help") == 0) {
        ok = text_puts(&output,
            "regions\nfuncs\nobjects\nframes\nnext\nfinish\nir [addr]\n"
            "vreg [selector:]N\nstats\ntrap\nhelp\n") != 0;
    } else if (strncmp(command, "ir", 2u) == 0 &&
               (command[2] == '\0' || command[2] == ' ')) {
        ok = monitor_ir_command(session, command[2] == ' ' ? command + 3 : "", &output);
    } else if (strncmp(command, "vreg", 4u) == 0 && command[4] == ' ') {
        ok = monitor_vreg(session, command + 5, &output);
    } else if (strcmp(command, "next") == 0 ||
               strcmp(command, "finish") == 0) {
        int finish = strcmp(command, "finish") == 0;
        if (session->state == TC_GDB_TERMINATED) {
            ok = text_puts(&output, "program is terminated\n") != 0;
        } else if (finish && session->vm->depth == 0u) {
            ok = 0;
        } else {
            uint32_t start_frame = session->vm->debug_frame;
            session->monitor_mode = finish ? TC_GDB_MONITOR_FINISH : TC_GDB_MONITOR_NEXT;
            session->monitor_frame = start_frame;
            session->monitor_have_target = 0;
            session->suppress_trap_reply = 1;
            if (breakpoint_present(session, session->vm->debug_pc)) {
                session->skip_breakpoint = 1;
                session->skip_address = session->vm->debug_pc;
            }
            result = run_instruction(session, 1);
            if (result == 0 && !session->stop_reason_interrupt &&
                session->state != TC_GDB_TERMINATED) {
                if (finish) {
                    if (session->vm->depth >= start_frame) {
                        result = continue_execution(session, 0, 0);
                    }
                } else if (session->vm->depth > start_frame) {
                    session->monitor_have_target = 1;
                    session->monitor_target_pc = session->vm->debug_pc;
                    result = continue_execution(session, 0, 0);
                }
            }
            session->monitor_mode = TC_GDB_MONITOR_NONE;
            session->monitor_have_target = 0;
            ok = result < 0 ? 0 : 1;
        }
    } else {
        ok = text_puts(&output, "unknown monitor command; use monitor help\n") != 0;
        result = -1;
    }
    if (session->exiting) {
        free(decoded);
        text_dispose(&output);
        return 0;
    }
    if (output.failed) {
        free(decoded);
        text_dispose(&output);
        return fail_oom(session);
    }
    if (!ok) {
        result = -1;
    }
    if (!send_monitor_text(session, &output)) {
        ok = 0;
    } else {
        ok = result == -1 ? send_error(session) : send_ok(session);
    }
    free(decoded);
    text_dispose(&output);
    return ok;
}

static int dispatch_packet(TcGdbSession *session, const uint8_t *data,
                           size_t len) {
    if (len == 0u) {
        return send_text(session, "");
    }
    if (data[0] == 0x03u) {
        return 1;
    }
    if (len >= 10u && memcmp(data, "qSupported", 10u) == 0) {
        return send_text(session,
            "PacketSize=4000;vContSupported+;QStartNoAckMode+;"
            "qXfer:features:read+;qXfer:memory-map:read+");
    }
    if (len >= 15u && memcmp(data, "QStartNoAckMode", 15u) == 0) {
        int ok = send_ok(session);
        if (ok) {
            session->no_ack = 1;
        }
        return ok;
    }
    if (data[0] == '!' && len == 1u) {
        session->extended = 1;
        return send_ok(session);
    }
    if (data[0] == '?' && len == 1u) {
        char reply[32];
        if (session->state == TC_GDB_TERMINATED) {
            snprintf(reply, sizeof(reply), "W%02x",
                     (unsigned)(session->vm->result->trapped
                                    ? session->profile->exit.trap_status
                                    : session->profile->exit.normal_status) & 255u);
            return send_text(session, reply);
        }
        return send_text(session, session->stop_reason_interrupt
                                     ? "T02thread:1;"
                                     : "T05thread:1;");
    }
    if (data[0] == 'g' && len == 1u) {
        return handle_regs_packet(session);
    }
    if (data[0] == 'G') {
        return send_error(session);
    }
    if (data[0] == 'p' && len > 1u) {
        return handle_register_read(session, data + 1, len - 1u);
    }
    if (data[0] == 'P' && len > 1u) {
        return handle_register_write(session, data + 1, len - 1u);
    }
    if (data[0] == 'm' && len > 1u) {
        return handle_memory_read(session, data + 1, len - 1u);
    }
    if (data[0] == 'M' && len > 1u) {
        return handle_memory_write(session, data + 1, len - 1u);
    }
    if (data[0] == 'X') {
        return send_error(session);
    }
    if ((data[0] == 'Z' || data[0] == 'z') && len > 1u) {
        if (data[1] == '0' && len > 3u && data[2] == ',') {
            return handle_breakpoint(session, data[0] == 'Z', data + 3, len - 3u);
        }
        if (data[1] >= '1' && data[1] <= '4') {
            return send_text(session, "");
        }
        return send_error(session);
    }
    if ((data[0] == 's' && len == 1u) || (data[0] == 'S' && len == 2u) ||
        (data[0] == 'c' && len == 1u) || (data[0] == 'C' && len == 2u)) {
        if (session->state == TC_GDB_TERMINATED) {
            return send_terminated_reply(session);
        }
        return handle_execute(session, data[0] == 's' || data[0] == 'S');
    }
    if (memcmp(data, "vCont", 5u) == 0) {
        if (session->state == TC_GDB_TERMINATED && len > 6u) {
            return send_terminated_reply(session);
        }
        return handle_vcont(session, data, len);
    }
    if (memcmp(data, "qXfer:", 6u) == 0) {
        return handle_q_xfer(session, data, len);
    }
    if (memcmp(data, "qRcmd,", 6u) == 0) {
        return handle_monitor(session, data, len);
    }
    if (data[0] == 'H' || memcmp(data, "qC", 2u) == 0 ||
        memcmp(data, "qfThreadInfo", 11u) == 0 ||
        memcmp(data, "qsThreadInfo", 11u) == 0 ||
        memcmp(data, "qAttached", 9u) == 0) {
        return handle_thread_packet(session, data, len);
    }
    if (len == 3u && data[0] == 'R' &&
        hex_digit_value((char)data[1]) >= 0 &&
        hex_digit_value((char)data[2]) >= 0) {
        uint32_t top;
        if (!session->extended || tc_gdb_journal_failed(session->journal)) {
            return send_error(session);
        }
        if (!tc_vm_reset(session->vm) ||
            !tc_vm_debug_push_top(session->vm, &top)) {
            session->fatal_io = session->vm->impl == TC_IMPL_IO;
            session->fatal_oom = session->vm->impl == TC_IMPL_OOM;
            session->exiting = 1;
            session->closing = 1;
            return send_error(session);
        }
        session->vm->debug_frame = top;
        session->vm->current_unit = session->vm->frames[top].unit;
        session->vm->current_bb = 0u;
        session->vm->current_insn = 0u;
        if (session->vm->current_unit->nblocks != 0u &&
            session->vm->current_unit->blocks[0].n != 0u) {
            session->vm->current_instr =
                &session->vm->current_unit->blocks[0].instrs[0];
            session->vm->current_span = session->vm->current_instr->span;
        }
        session->state = TC_GDB_STOPPED;
        session->stop_reason_interrupt = 0;
        heap_patches_clear(session);
        session->monitor_mode = TC_GDB_MONITOR_NONE;
        session->monitor_have_target = 0;
        session->skip_breakpoint = 0;
        if (session->vm->stats != NULL) {
            session->vm->stats->region_peak[TC_REG_CODE] =
                ((uint64_t)session->vm->ir->nfuncs + 1u) * 16u;
            for (size_t i = 0u; i < session->vm->ir->nobjects; i++) {
                const TcObject *object = &session->vm->ir->objects[i];
                if (object->region == TC_REG_STATIC &&
                    object->offset + object->size >
                        session->vm->stats->region_peak[TC_REG_STATIC]) {
                    session->vm->stats->region_peak[TC_REG_STATIC] =
                        object->offset + object->size;
                }
            }
        }
        tc_vm_trace_header(session->vm);
        return 1;
    }
    if (data[0] == 'D' && len == 1u) {
        if (session->extended) {
            return session->state == TC_GDB_RUNNING ? 1 : send_error(session);
        }
        clear_breakpoints(session);
        if (session->state == TC_GDB_TERMINATED) {
            session->exiting = 1;
            session->closing = 1;
            return send_ok(session);
        }
        session->detached = 1;
        session->closing = 1;
        return send_ok(session);
    }
    if (data[0] == 'k' && len == 1u) {
        session->exiting = 1;
        session->closing = 1;
        return 1;
    }
    if (memcmp(data, "vKill", 5u) == 0 && len == 5u) {
        session->exiting = 1;
        session->closing = 1;
        return send_ok(session);
    }
    return send_text(session, "");
}

static void close_client(TcGdbSession *session) {
    if (session->client >= 0) {
        close(session->client);
    }
    session->client = -1;
    session->connected = 0;
    session->rx_len = 0u;
    session->frame_len = 0u;
    session->discarding = 0;
    session->no_ack = 0;
    session->extended = 0;
    session->bad_checksums = 0;
    session->thread_listed = 0;
    session->skip_breakpoint = 0;
    session->monitor_mode = TC_GDB_MONITOR_NONE;
    session->monitor_have_target = 0;
}

static int service_nonblocking(TcGdbSession *session) {
    uint8_t bytes[4096];
    for (;;) {
        ssize_t count = recv(session->client, bytes, sizeof(bytes), MSG_DONTWAIT);
        size_t pos = 0u;
        if (count > 0) {
            while (pos < (size_t)count && session->connected &&
                   !session->detached && !session->exiting) {
                uint8_t ch = bytes[pos++];
                if (ch == 0x03u) {
                    if (session->state == TC_GDB_RUNNING) {
                        session->stop_reason_interrupt = 1;
                    }
                    continue;
                }
                if (session->discarding) {
                    if (session->frame_len == 0u) {
                        if (ch == '#') {
                            session->frame_len = 1u;
                        }
                    } else {
                        session->frame_len++;
                        if (session->frame_len == 3u) {
                            session->frame_len = 0u;
                            session->discarding = 0;
                        }
                    }
                    continue;
                }
                if (session->frame_len == 0u) {
                    if (ch != '$') {
                        continue;
                    }
                    session->frame[0] = '$';
                    session->frame_len = 1u;
                    session->frame_hash = SIZE_MAX;
                    continue;
                }
                if (session->frame_len >= sizeof(session->frame)) {
                    session->discarding = 1;
                    session->frame_len = 0u;
                    continue;
                }
                session->frame[session->frame_len++] = ch;
                if (ch == '#') {
                    session->frame_hash = session->frame_len - 2u;
                }
                if (session->frame_hash != SIZE_MAX &&
                    session->frame_len >= 3u &&
                    session->frame_len == session->frame_hash + 4u) {
                    TcGdbRspPacket packet;
                    TcGdbRspDecodeStatus status =
                        tc_gdb_rsp_decode(session->frame, session->frame_len, &packet);
                    if (status == TC_GDB_RSP_DECODE_OK) {
                        session->bad_checksums = 0;
                        if (!send_ack(session, '+')) {
                            return 0;
                        }
                        if (session->state == TC_GDB_RUNNING) {
                            if ((packet.len == 1u && packet.data[0] == 'k') ||
                                (packet.len == 5u && memcmp(packet.data, "vKill", 5u) == 0) ||
                                (packet.len == 1u && packet.data[0] == 'D' &&
                                 !session->extended)) {
                                (void)dispatch_packet(session, packet.data, packet.len);
                            }
                        } else {
                            (void)dispatch_packet(session, packet.data, packet.len);
                        }
                    } else if (status == TC_GDB_RSP_DECODE_TOO_LONG) {
                        session->frame_len = 0u;
                        session->frame_hash = SIZE_MAX;
                        session->bad_checksums = 0;
                    } else {
                        session->bad_checksums++;
                        if (!send_ack(session, '-')) {
                            return 0;
                        }
                        if (session->bad_checksums >= 3) {
                            session->closing = 1;
                            session->connected = 0;
                        }
                    }
                    session->frame_len = 0u;
                    session->frame_hash = SIZE_MAX;
                }
            }
            continue;
        }
        if (count == 0) {
            session->connected = 0;
            session->closing = 1;
            if (session->state == TC_GDB_RUNNING) {
                session->stop_reason_interrupt = 1;
            }
            return 0;
        }
        if (errno == EINTR) {
            if (tc_gdb_host_interrupt) {
                session->exiting = 1;
                return 0;
            }
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        session->connected = 0;
        session->closing = 1;
        if (session->state == TC_GDB_RUNNING) {
            session->stop_reason_interrupt = 1;
        }
        return 0;
    }
}

static int journal_write(void *ctx, const uint8_t *bytes, size_t size) {
    TcGdbSession *session = (TcGdbSession *)ctx;
    return tc_gdb_journal_write(session->journal, bytes, size) == 0 ? 0 : -1;
}

static int journal_flush(void *ctx) {
    (void)ctx;
    return 0;
}

static int gdb_stdin_read(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                          TcIoStatus *status) {
    TcGdbSession *session = (TcGdbSession *)ctx;
    *n = 0u;
    *status = TC_IO_OK;
    for (;;) {
        struct pollfd fds[2];
        int ready;
        if (session->exiting || tc_gdb_host_interrupt) {
            session->exiting = 1;
            *status = TC_IO_ERROR;
            return -1;
        }
        if (session->client >= 0 && session->connected && !session->detached) {
            if (!service_nonblocking(session)) {
                if (session->exiting) {
                    *status = TC_IO_ERROR;
                    return -1;
                }
                session->input_paused = 1;
                *status = TC_IO_AGAIN;
                return 0;
            }
            if (session->stop_reason_interrupt) {
                session->vm->debug_stop |= TC_VM_DEBUG_INTERRUPT;
                session->input_paused = 1;
                *status = TC_IO_AGAIN;
                return 0;
            }
            if (session->detached) {
                session->input_paused = 1;
                *status = TC_IO_AGAIN;
                return 0;
            }
        }
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = !session->detached && session->connected ? session->client : -1;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        ready = poll(fds, 2u, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                if (tc_gdb_host_interrupt) {
                    session->exiting = 1;
                    *status = TC_IO_ERROR;
                    return -1;
                }
                continue;
            }
            *status = TC_IO_ERROR;
            return -1;
        }
        if (fds[1].revents != 0u) {
            if (!service_nonblocking(session)) {
                if (session->exiting) {
                    *status = TC_IO_ERROR;
                    return -1;
                }
                session->input_paused = 1;
                *status = TC_IO_AGAIN;
                return 0;
            }
            if (session->stop_reason_interrupt || session->closing ||
                session->detached) {
                session->vm->debug_stop |= TC_VM_DEBUG_INTERRUPT;
                session->input_paused = 1;
                *status = TC_IO_AGAIN;
                return 0;
            }
        }
        if (fds[0].revents != 0u) {
            ssize_t count = read(STDIN_FILENO, buf, cap);
            if (count > 0) {
                *n = (size_t)count;
                *status = TC_IO_OK;
                return 0;
            }
            if (count == 0) {
                *status = TC_IO_EOF;
                return 0;
            }
            if (errno != EINTR) {
                *status = TC_IO_ERROR;
                return -1;
            }
        }
    }
}

static unsigned debug_boundary(void *ctx, TcVm *vm, uint32_t frame_index,
                               uint64_t address) {
    TcGdbSession *session = (TcGdbSession *)ctx;
    unsigned flags = 0u;
    int skip_current = 0;
    (void)vm;
    if (session->detached || session->exiting) {
        return session->exiting ? TC_VM_DEBUG_DISCONNECT : 0u;
    }
    if (session->client >= 0 && session->connected && !session->closing) {
        (void)service_nonblocking(session);
    }
    if (session->detached) {
        return TC_VM_DEBUG_DISCONNECT;
    }
    if (session->exiting) {
        return TC_VM_DEBUG_DISCONNECT;
    }
    if (session->closing || !session->connected) {
        session->stop_reason_interrupt = 1;
        return TC_VM_DEBUG_INTERRUPT;
    }
    if (session->skip_breakpoint) {
        session->skip_breakpoint = 0;
        skip_current = address == session->skip_address;
    }
    if (session->stop_reason_interrupt) {
        flags |= TC_VM_DEBUG_INTERRUPT;
    }
    if (session->monitor_mode == TC_GDB_MONITOR_NEXT) {
        if (session->monitor_have_target) {
            if (frame_index == session->monitor_frame &&
                address == session->monitor_target_pc) {
                flags |= TC_VM_DEBUG_STOP;
            }
        }
    } else if (session->monitor_mode == TC_GDB_MONITOR_FINISH) {
        if (frame_index < session->monitor_frame) {
            flags |= TC_VM_DEBUG_STOP;
        }
    }
    if (!vm->debug_single && !skip_current &&
        breakpoint_present(session, address)) {
        flags |= TC_VM_DEBUG_STOP;
    }
    if (flags != 0u) {
        session->stop_reason_interrupt =
            (flags & TC_VM_DEBUG_INTERRUPT) != 0u;
    }
    return flags;
}

static int accept_client(TcGdbSession *session) {
    while (!session->exiting) {
        if (tc_gdb_host_interrupt) {
            session->exiting = 1;
            return 0;
        }
        session->client = accept(session->listener, NULL, NULL);
        if (session->client >= 0) {
            session->connected = 1;
            session->closing = 0;
            session->no_ack = 0;
            session->extended = 0;
            session->bad_checksums = 0;
            session->rx_len = 0u;
            session->frame_len = 0u;
            session->discarding = 0;
            return 1;
        }
        if (errno == EINTR) {
            if (tc_gdb_host_interrupt) {
                session->exiting = 1;
                return 0;
            }
            continue;
        }
        return 0;
    }
    return 0;
}

static int run_free(TcGdbSession *session) {
    TcVmFlow flow;
    const TcVmDebugHooks *saved_debug = session->vm->debug;
    uint32_t frame_index;
    session->detached = 1;
    if (session->state == TC_GDB_TERMINATED) {
        return 1;
    }
    frame_index = session->vm->debug_frame;
    session->vm->debug = NULL;
    session->vm->debug_single = 0;
    session->vm->debug_stop = 0u;
    flow = tc_vm_execute_resume(session->vm, frame_index, 0);
    session->vm->debug = saved_debug;
    session->state = TC_GDB_TERMINATED;
    session->vm->debug_terminated = 1;
    if (flow == TC_VM_FLOW_FAIL) {
        if (session->vm->result->trapped) {
            session->vm->debug_frame = session->vm->nframes - 1u;
            session->vm->debug_pc = tc_vm_debug_pc(session->vm);
        } else {
            session->exiting = 1;
            return 0;
        }
    } else {
        session->vm->debug_frame = UINT32_MAX;
        session->vm->debug_pc = session->vm->debug_code_base +
                                (uint64_t)session->vm->debug_linemap->count * 4u;
        if (session->vm->stats != NULL) {
            session->vm->stats->trapped = 0;
        }
    }
    tc_vm_trace_footer(session->vm,
                       session->vm->result->trapped ? "trap" : "ok");
    return 1;
}

static int open_listener(TcGdbSession *session, unsigned requested) {
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int yes = 1;
    session->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (session->listener < 0) {
        return 0;
    }
    (void)setsockopt(session->listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)requested);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(session->listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(session->listener, 1) != 0 ||
        getsockname(session->listener, (struct sockaddr *)&address, &length) != 0) {
        close(session->listener);
        session->listener = -1;
        return 0;
    }
    session->port = ntohs(address.sin_port);
    return 1;
}

static void init_stats(TcGdbSession *session, TcVmStats *stats) {
    size_t i;
    memset(stats, 0, sizeof(*stats));
    stats->region_peak[TC_REG_CODE] = ((uint64_t)session->vm->ir->nfuncs + 1u) * 16u;
    for (i = 0u; i < session->vm->ir->nobjects; i++) {
        const TcObject *object = &session->vm->ir->objects[i];
        if (object->region == TC_REG_STATIC &&
            object->offset + object->size > stats->region_peak[TC_REG_STATIC]) {
            stats->region_peak[TC_REG_STATIC] = object->offset + object->size;
        }
    }
}

TcVmStatus tc_vm_run_gdb(const TcIR *ir, const TcPlatformProfile *profile,
                         TcVmResult *out_result, TcImplError *out_impl,
                         const TcVmObserver *observer, TcVmStats *out_stats,
                         const TcLineMap *linemap,
                         const TcGdbOptions *options) {
    TcVm vm;
    TcGdbSession session;
    TcVmStatus status = TC_VM_STATUS_IMPL;
    uint32_t top_frame = 0u;
    uint64_t code_size;
    int initialized = 0;
    int listener_ready = 0;
    int completed = 0;

    memset(&vm, 0, sizeof(vm));
    memset(&session, 0, sizeof(session));
    session.listener = -1;
    session.client = -1;
    if (options == NULL || linemap == NULL) {
        if (out_impl != NULL) {
            *out_impl = TC_IMPL_INTERNAL;
        }
        return TC_VM_STATUS_IMPL;
    }
    if (!tc_vm_setup(&vm, ir, profile, out_result, out_impl)) {
        tc_vm_teardown(&vm);
        return TC_VM_STATUS_IMPL;
    }
    initialized = 1;
    session.vm = &vm;
    session.profile = profile;
    session.options = options;
    session.state = TC_GDB_STOPPED;
    vm.debug = &session.hooks;
    vm.debug_linemap = linemap;
    vm.debug_code_base = vm.W == 64u ? TC_DEBUG_CODE_BASE64 : TC_DEBUG_CODE_BASE32;
    if (tc_vm_debug_code_size(profile, linemap, &code_size) != TC_IMPL_OK) {
        tc_vm_impl(&vm, TC_IMPL_INTERNAL);
        goto done;
    }
    if (out_stats == NULL) {
        tc_vm_impl(&vm, TC_IMPL_INTERNAL);
        goto done;
    }
    vm.stats = out_stats;
    init_stats(&session, out_stats);
    if (observer != NULL) {
        vm.observer = observer;
        if (observer->trace != NULL && observer->linemap == NULL) {
            tc_vm_impl(&vm, TC_IMPL_INTERNAL);
            goto done;
        }
    }
    session.local_profile = *profile;
    session.local_io = *profile->io;
    session.local_out.write = journal_write;
    session.local_out.flush = journal_flush;
    session.local_out.ctx = &session;
    session.local_in.read = gdb_stdin_read;
    session.local_in.ctx = &session;
    session.local_io.out = &session.local_out;
    session.local_io.in = &session.local_in;
    session.local_profile.io = &session.local_io;
    session.profile = &session.local_profile;
    vm.profile = &session.local_profile;
    vm.input.src = &session.local_in;
    session.hooks.ctx = &session;
    session.hooks.boundary = debug_boundary;

    {
        struct sigaction action;
        struct sigaction ignore;
        memset(&action, 0, sizeof(action));
        memset(&ignore, 0, sizeof(ignore));
        action.sa_handler = host_interrupt;
        sigemptyset(&action.sa_mask);
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        sigaction(SIGINT, &action, NULL);
        sigaction(SIGPIPE, &ignore, NULL);
    }

    if (options->stdout_journal == NULL) {
        vm.impl = TC_IMPL_IO;
        goto done;
    }
    {
        TcImplError error = tc_gdb_journal_open(&session.journal,
                                                 options->stdout_journal);
        if (error != TC_IMPL_OK) {
            vm.impl = error;
            goto done;
        }
    }
    if (!open_listener(&session, options->gdb_port)) {
        vm.impl = errno == EADDRINUSE ? TC_IMPL_IO : TC_IMPL_UNAVAILABLE;
        goto done;
    }
    listener_ready = 1;
    fprintf(stderr, "tc-vm: gdbstub on 127.0.0.1:%" PRIu64 "\n", session.port);
    if (fflush(stderr) != 0) {
        vm.impl = TC_IMPL_IO;
        goto done;
    }
    if (!tc_vm_debug_push_top(&vm, &top_frame)) {
        goto done;
    }
    vm.debug_pc = vm.debug_code_base;
    vm.debug_frame = top_frame;
    tc_vm_trace_header(&vm);

    while (!session.exiting) {
        int ordinary_terminated;
        if (!accept_client(&session)) {
            if (session.exiting) {
                break;
            }
            vm.impl = TC_IMPL_IO;
            goto done;
        }
        while (!session.exiting && session.connected && !session.detached) {
            if (!service_nonblocking(&session)) {
                break;
            }
            if (session.state == TC_GDB_RUNNING) {
                session.state = TC_GDB_STOPPED;
            }
            if (session.connected && session.client >= 0 && !session.detached) {
                struct pollfd pfd;
                pfd.fd = session.client;
                pfd.events = POLLIN;
                pfd.revents = 0;
                if (poll(&pfd, 1u, -1) < 0) {
                    if (errno == EINTR) {
                        if (tc_gdb_host_interrupt) {
                            session.exiting = 1;
                            break;
                        }
                        continue;
                    }
                    session.connected = 0;
                    break;
                }
                if (tc_gdb_host_interrupt) {
                    session.exiting = 1;
                    break;
                }
            }
        }
        ordinary_terminated = session.state == TC_GDB_TERMINATED && !session.extended;
        close_client(&session);
        if (session.detached) {
            if (!run_free(&session)) {
                goto done;
            }
            completed = 1;
            break;
        }
        if (ordinary_terminated) {
            /* An ordinary target-remote session owns process exit after
             * termination, including a client-side disconnect. */
            completed = 1;
            break;
        }
    }
    if (session.state != TC_GDB_TERMINATED && !completed && !session.exiting) {
        vm.impl = TC_IMPL_INTERNAL;
        goto done;
    }
    completed = 1;

done:
    if (session.state == TC_GDB_TERMINATED || session.exiting) {
        completed = 1;
    }
    if (vm.trace_started && !vm.debug_terminated && !vm.trace_failed &&
        vm.observer != NULL && vm.observer->trace != NULL) {
        /* An abandoned stopped session has no execution footer. */
    }
    if (session.fatal_oom) {
        vm.impl = TC_IMPL_OOM;
    } else if (vm.impl == TC_IMPL_OK && session.fatal_io) {
        vm.impl = TC_IMPL_IO;
    }
    if (vm.count_overflow) {
        vm.impl = TC_IMPL_INTERNAL;
    }
    if (vm.trace_failed && vm.impl == TC_IMPL_OK) {
        vm.impl = TC_IMPL_IO;
    }
    heap_patches_clear(&session);
    if (initialized) {
        tc_vm_teardown(&vm);
    }
    if (session.listener >= 0) {
        close(session.listener);
    }
    if (session.client >= 0) {
        close(session.client);
    }
    tc_gdb_journal_close(session.journal);
    if (vm.impl != TC_IMPL_OK || !completed) {
        status = TC_VM_STATUS_IMPL;
        if (out_impl != NULL && *out_impl == TC_IMPL_OK) {
            *out_impl = vm.impl != TC_IMPL_OK ? vm.impl : TC_IMPL_INTERNAL;
        }
    } else {
        status = vm.result->trapped ? TC_VM_STATUS_TRAP : TC_VM_STATUS_OK;
    }
    (void)listener_ready;
    return status;
}

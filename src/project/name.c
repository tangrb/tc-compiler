#include "project/name.h"

#include <stdint.h>
#include <string.h>

static int next_utf8(const unsigned char **cursor, uint32_t *codepoint) {
    const unsigned char *bytes = *cursor;
    uint32_t value;
    size_t length;
    size_t i;
    if (bytes[0] < 0x80u) {
        *codepoint = bytes[0];
        *cursor = bytes + 1u;
        return 1;
    }
    if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) {
        value = bytes[0] & 0x1fu;
        length = 2;
    } else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) {
        value = bytes[0] & 0x0fu;
        length = 3;
    } else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) {
        value = bytes[0] & 0x07u;
        length = 4;
    } else {
        return 0;
    }
    for (i = 1; i < length; i++) {
        if ((bytes[i] & 0xc0u) != 0x80u) {
            return 0;
        }
        value = (value << 6) | (bytes[i] & 0x3fu);
    }
    if ((length == 3 && value < 0x800u) ||
        (length == 4 && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return 0;
    }
    *codepoint = value;
    *cursor = bytes + length;
    return 1;
}

static int forbidden_project_codepoint(uint32_t codepoint) {
    return codepoint == '/' || codepoint == '\\' || codepoint == '"' ||
           codepoint == 0x2028u || codepoint == 0x2029u ||
           (codepoint <= 0x1fu) || (codepoint >= 0x7fu && codepoint <= 0x9fu) ||
           codepoint == 0x00adu ||
           (codepoint >= 0x0600u && codepoint <= 0x0605u) ||
           codepoint == 0x061cu || codepoint == 0x06ddu || codepoint == 0x070fu ||
           (codepoint >= 0x0890u && codepoint <= 0x0891u) ||
           codepoint == 0x08e2u || codepoint == 0x180eu ||
           (codepoint >= 0x200bu && codepoint <= 0x200fu) ||
           (codepoint >= 0x202au && codepoint <= 0x202eu) ||
           (codepoint >= 0x2060u && codepoint <= 0x2064u) ||
           (codepoint >= 0x2066u && codepoint <= 0x206fu) ||
           codepoint == 0xfeffu ||
           (codepoint >= 0xfff9u && codepoint <= 0xfffbu) ||
           codepoint == 0x110bdu || codepoint == 0x110cdu ||
           (codepoint >= 0x13430u && codepoint <= 0x1343fu) ||
           (codepoint >= 0x1bca0u && codepoint <= 0x1bca3u) ||
           (codepoint >= 0x1d173u && codepoint <= 0x1d17au) ||
           codepoint == 0xe0001u ||
           (codepoint >= 0xe0020u && codepoint <= 0xe007fu);
}

int tc_project_name_valid(const char *name) {
    const unsigned char *cursor;
    uint32_t codepoint;
    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        return 0;
    }
    cursor = (const unsigned char *)name;
    while (*cursor != '\0') {
        if (!next_utf8(&cursor, &codepoint) ||
            forbidden_project_codepoint(codepoint)) {
            return 0;
        }
    }
    return 1;
}

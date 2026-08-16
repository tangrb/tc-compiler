/*
 * tc_test_port.c — 单元测试跨平台移植 shim 实现（见 tc_test_port.h）
 *
 * POSIX（glibc / macOS）：转发系统 dlfcn 与 mkstemps。
 * Windows（MinGW-w64，无 dlfcn.h / mkstemps）：
 *   - dlopen/dlsym/dlclose → LoadLibraryA/GetProcAddress/FreeLibrary；
 *   - mkstemps → _mktemp_s 生成唯一前缀 + 拼接后缀 + _open 独占创建。
 */
#include "tc_test_port.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h> /* macOS 在 unistd.h 声明 mkstemps，glibc 在 stdlib.h */
#endif

void *tc_test_dlopen(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void *tc_test_dlsym(void *handle, const char *symbol) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}

int tc_test_dlclose(void *handle) {
#ifdef _WIN32
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
#else
    return dlclose(handle);
#endif
}

int tc_test_mkstemps(char *tmpl, int suffixlen) {
#ifdef _WIN32
    /*
     * msvcrt 的 _mktemp_s/_open 不认 msys2 的 POSIX /tmp 前缀（无盘符），
     * 且测试模板数组按 '/tmp/...' 尺寸分配，不能容纳 Windows 绝对临时
     * 路径。因此把模板开头的 '/tmp/' 原位改写成 './'（更短、不越界），
     * 得到相对当前目录（CI 仓库根，可写）的模板，再用 _mktemp_s+_open。
     */
    size_t len;
    size_t xpos;
    char suffix[32];
    int fd;

    if (tmpl == NULL || suffixlen < 0 || suffixlen > 16) {
        errno = EINVAL;
        return -1;
    }
    /* '/tmp/' → './'：原位改写，缩短 3 字节，绝不越界 */
    if (strncmp(tmpl, "/tmp/", 5) == 0) {
        tmpl[0] = '.';
        tmpl[1] = '\0';
        memmove(tmpl + 1, tmpl + 5, strlen(tmpl + 5) + 1);
    }
    len = strlen(tmpl);
    if (len < (size_t)suffixlen + 6 ||
        strncmp(tmpl + len - (size_t)suffixlen - 6, "XXXXXX", 6) != 0) {
        errno = EINVAL;
        return -1;
    }
    xpos = len - (size_t)suffixlen - 6;
    memcpy(suffix, tmpl + xpos + 6, (size_t)suffixlen + 1); /* 保存后缀（含 NUL） */
    tmpl[xpos + 6] = '\0';                                  /* 截断成 baseXXXXXX */
    if (_mktemp_s(tmpl, xpos + 7) != 0) {
        return -1;
    }
    memcpy(tmpl + xpos + 6, suffix, (size_t)suffixlen + 1); /* 恢复后缀 */
    fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    return fd;
#else
    return mkstemps(tmpl, suffixlen);
#endif
}

char *tc_test_mkdtemp(char *tmpl) {
#ifdef _WIN32
    size_t len;
    size_t xpos;
    unsigned int seed;
    unsigned int attempt;
    unsigned int rnd;

    if (tmpl == NULL || strlen(tmpl) < 6) {
        errno = EINVAL;
        return NULL;
    }
    /* '/tmp/' → './'：原位改写，不越界（见 tc_test_mkstemps 注释） */
    if (strncmp(tmpl, "/tmp/", 5) == 0) {
        tmpl[0] = '.';
        tmpl[1] = '\0';
        memmove(tmpl + 1, tmpl + 5, strlen(tmpl + 5) + 1);
    }
    len = strlen(tmpl);
    xpos = len - 6;
    if (strncmp(tmpl + xpos, "XXXXXX", 6) != 0) {
        errno = EINVAL;
        return NULL;
    }
    seed = (unsigned int)GetCurrentProcessId();
    for (attempt = 0; attempt < 100; attempt++) {
        rnd = seed ^ (attempt * 2654435761u);
        (void)snprintf(tmpl + xpos, 7, "%06x", rnd & 0xFFFFFFu);
        if (_mkdir(tmpl) == 0) {
            return tmpl;
        }
        if (errno != EEXIST) {
            return NULL;
        }
    }
    errno = EEXIST;
    return NULL;
#else
    return mkdtemp(tmpl);
#endif
}

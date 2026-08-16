/*
 * tc_test_port.h — 单元测试跨平台移植 shim
 *
 * MinGW-w64 不提供 <dlfcn.h> 与 mkstemps()，而 glibc / macOS 均提供。
 * 本模块把这两处平台差异收敛到一处：POSIX 分支直接转发系统实现，
 * _WIN32 分支用 LoadLibrary/GetProcAddress 与 _mktemp_s 等价实现，
 * 测试代码保持平台无关（统一调用 tc_test_* 前缀）。
 */
#ifndef TC_TEST_PORT_H
#define TC_TEST_PORT_H

/* dlopen 等价：加载共享库（Windows: LoadLibraryA，立即加载）。失败返回 NULL。 */
void *tc_test_dlopen(const char *path);

/* dlsym 等价：取符号地址（Windows: GetProcAddress）。未找到返回 NULL。 */
void *tc_test_dlsym(void *handle, const char *symbol);

/* dlclose 等价：卸载共享库（Windows: FreeLibrary）。成功返回 0。 */
int tc_test_dlclose(void *handle);

/*
 * mkstemps 等价：创建带后缀的唯一临时文件并返回 fd（成功后模板被改写为
 * 实际路径，形如 nameXXXXXX.c → namea1b2c3.c）。模板末尾必须形如 XXXXXX<suffix>。
 * 失败返回 -1 并设置 errno。
 */
int tc_test_mkstemps(char *tmpl, int suffixlen);

/*
 * mkdtemp 等价：创建唯一临时目录并返回模板路径（成功后模板被改写为实际路径）。
 * 上游 MinGW-w64 无 mkdtemp（MSYS2 打过补丁），故 Windows 用 _mkdir + 唯一名
 * 循环实现；POSIX 分支直接转发系统 mkdtemp。失败返回 NULL 并设置 errno。
 */
char *tc_test_mkdtemp(char *tmpl);

#endif /* TC_TEST_PORT_H */

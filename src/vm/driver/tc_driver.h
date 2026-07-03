/*
 * tc_driver.h — TC-VM 驱动层接口
 *
 * 驱动层串联完整流水线：读源 → 逐行 Lex+Parse → Analyze →（可选）Execute。
 * 对外提供基于字符串或文件路径的入口，供 main.c 及测试脚本调用。
 */
#ifndef TC_DRIVER_H
#define TC_DRIVER_H

#include "tc_types.h"

/**
 * 从内存中的源字符串运行 TC 程序。
 * @param source     源字符串
 * @param check_only 为真时仅做静态分析，不执行
 * @param diag       诊断对象
 * @return 成功返回 0；失败返回 -1 并设置 diag
 */
int tc_run_source(const char *source, int check_only, TcDiagnostic *diag);

/**
 * 从文件路径读取源码并运行 TC 程序。
 * @param path       源文件路径
 * @param check_only 仅静态分析标志
 * @param diag       诊断对象
 * @return 成功返回 0；文件 I/O 或运行失败返回 -1 并设置 diag
 */
int tc_run_file(const char *path, int check_only, TcDiagnostic *diag);

#endif

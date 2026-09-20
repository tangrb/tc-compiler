/*
 * tc_diagnostic.h — 错误诊断接口
 *
 * Lexer / Parser / Analyzer / Executor / AOT / Embed 通过 TcDiagnostic
 * 统一报告错误。单槽 fail-fast，仅保留第一条。
 * 诊断由 driver / Embed 宿主格式化输出（类 GCC 的 file:line:col）。
 */
#ifndef TC_DIAGNOSTIC_H
#define TC_DIAGNOSTIC_H

#include <stdio.h>

#include "tc_types.h"

/**
 * 可移植 strndup：复制字符串至多 n 字节并保证 NUL 结尾。
 * glibc / MSYS2 提供 strndup，但上游 MinGW-w64 无此函数（隐式声明在
 * GCC 14+ 下为硬错误），故统一走本实现，避免平台差异。
 * @param s 源串（NULL 返回 NULL）
 * @param n 最大复制字节数（遇 NUL 提前停止）
 * @return 新分配副本；分配失败返回 NULL
 */
char *tc_strndup(const char *s, size_t n);

/** 初始化诊断结构为默认空状态 */
void tc_diagnostic_init(TcDiagnostic *diag);

/**
 * 释放诊断模块管理的文本字段（message / filename / snippet / source）并清空位置信息。
 * @note 可重复调用；每次 tc_diagnostic_set 前无需手动清除。
 */
void tc_diagnostic_clear(TcDiagnostic *diag);

/**
 * 绑定诊断所对应的源文件路径与完整源文本。
 * @param diag     诊断对象
 * @param filename 源文件路径（内部 strdup 复制）
 * @param source   完整源文本（内部 strdup 复制；NULL 表示无源文本）
 * @return 成功返回 0；文本分配失败返回 -1 并设置 Implementation/OutOfMemory
 */
int tc_diagnostic_set_source(TcDiagnostic *diag, const char *filename, const char *source);

/**
 * 仅当目标 (filename, source) 与当前绑定不同时切换（避免整篇源码被反复复制）。
 * 供多文件编译在「入口 ↔ 依赖模块」之间切换诊断定位使用。
 * @return 成功返回 0；文本分配失败返回 -1
 */
int tc_diagnostic_use_source(TcDiagnostic *diag, const char *filename, const char *source);

/**
 * 读取当前绑定的源文件路径与源文本。
 * @param diag      诊断对象
 * @param filename  输出：内部存储的路径指针（NULL 表示未绑定；不得释放）
 * @param source    输出：内部存储的完整源文本指针（可为 NULL；不得释放）
 */
void tc_diagnostic_get_source(const TcDiagnostic *diag, const char **filename,
                              const char **source);

/**
 * 设置一条新的诊断信息。
 * @param diag    诊断对象
 * @param kind    错误种类
 * @param line    出错行号（1-based），0 表示无行号
 * @param column  出错列号（1-based），可传入 TC_COLUMN_UNKNOWN
 * @param message 错误描述（内部 strdup 复制）
 * @return 成功返回 0；文本分配失败返回 -1 并设置 Implementation/OutOfMemory
 * @note 若 diag->source 可用且 line > 0，同时捕获出错行源码到 snippet 字段
 */
int tc_diagnostic_set(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                      const char *message);

/** 设置 API/环境域诊断；成功返回 0，文本分配失败返回 -1 并改设 OOM。 */
int tc_diagnostic_set_api(TcDiagnostic *diag, TcApiErrorCode code, const char *message);

/**
 * 是否已存在真实诊断（domain != TC_DIAG_NONE）。
 * @return 已设置返回 1；空诊断返回 0
 */
int tc_diagnostic_is_set(const TcDiagnostic *diag);

/**
 * 挂起一条 CT 类诊断（语言标准 §11「阶段优先」）。
 *
 * 常量求值与静态三态判定属 CT 类阶段。§11 要求首个规范诊断按
 * 「LT → SYN → SEM → CT」的阶段顺序选取，故 CT 类诊断必须先挂起，
 * 待 SEM 类检查（可达性、确定初始化、调用图等）全部无触发后，
 * 再由 tc_diagnostic_flush_deferred 发布。仅保留源序位置最靠前的一条
 * （§11 第 2 条：行号升序，同行按 Token 次序）。
 *
 * @param diag    诊断对象
 * @param kind    错误种类（CT 类码）
 * @param line    出错行号
 * @param column  出错列号
 * @param message 错误描述（内部 strdup 复制）
 * @return 始终返回 -1，便于调用方沿用既有「失败」控制流
 */
int tc_diagnostic_defer(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                        const char *message);

/** 是否存在挂起的 CT 类诊断。 */
int tc_diagnostic_has_deferred(const TcDiagnostic *diag);

/**
 * 发布挂起的 CT 类诊断（若存在）。
 * 仅在全部 SEM 类检查均无触发、且当前无真实诊断时调用。
 * @return 发布了诊断返回 1；无挂起诊断返回 0
 */
int tc_diagnostic_flush_deferred(TcDiagnostic *diag);

/** 丢弃挂起的 CT 类诊断（已有更高优先级的 SEM 类诊断时）。 */
void tc_diagnostic_drop_deferred(TcDiagnostic *diag);

/** 释放挂起诊断占用的文本字段并复位（不改变其它状态）。 */
void tc_diagnostic_clear_deferred(TcDiagnostic *diag);

/* ── 挂起的 SEM 类诊断（B-40，语言标准 §11「阶段优先」） ── */

/**
 * 挂起一条 SEM 类诊断，供解析器在「形态合法但静态语义拒绝」时沿用失败控制流
 * 而不打断解析（否则更晚的语法错误会被更早的 SEM 诊断掩盖）。
 *
 * 仅保留源序位置最靠前的一条（§11 第 2 条）。调用方**不得**因此中止解析：
 * 语法层须继续走完整个文件，SYN 类诊断仍按既有 fail-fast 立即报告。
 *
 * @return 成功（无论是否替换已有挂起项）返回 0；内存不足返回 -1
 */
int tc_diagnostic_defer_sem(TcDiagnostic *diag, TcErrorKind kind, int line, int column,
                            const char *message);

/** 是否存在挂起的 SEM 类诊断。 */
int tc_diagnostic_has_deferred_sem(const TcDiagnostic *diag);

/**
 * 在 SEM 阶段发布挂起的 SEM 类诊断。
 *
 * - 当前无真实诊断：发布挂起项并返回 1；
 * - 已有同一源文件的真实诊断且挂起项位置更靠前：替换并返回 1；
 * - 否则（已有更早/其它阶段/其它文件的诊断）丢弃挂起项并返回 0。
 */
int tc_diagnostic_publish_deferred_sem(TcDiagnostic *diag);

/** 释放挂起的 SEM 类诊断（不改变其它状态）。 */
void tc_diagnostic_clear_deferred_sem(TcDiagnostic *diag);

/**
 * 将诊断信息格式化输出到指定流。
 * @param diag 诊断对象
 * @param out  输出流（通常为 stderr）
 *
 * 输出格式：
 *   <file>:<line>:<column>: error: <message>
 *   <file>:<line>: error: <message>          （无列号）
 *   <file>: error: <message>                  （无行号）
 *   随后附加出错行源码与 ^ 指示符（若有 snippet）
 */
void tc_diagnostic_print(const TcDiagnostic *diag, FILE *out);

/* 同 tc_diagnostic_print，但普通诊断首行附错误码名（CLI --print-error-code）。 */
void tc_diagnostic_print_with_code(const TcDiagnostic *diag, FILE *out);

#ifdef TC_DIAGNOSTIC_TESTING
/** 单元测试专用：在指定成功分配次数后令诊断文本复制失败；负数关闭。 */
void tc_diagnostic_test_fail_alloc_after(int successful_allocations);
#endif

#endif

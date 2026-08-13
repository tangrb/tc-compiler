/*
 * tc_analyze_6e.h — I/O 格式检查
 *
 * 校验 write/read 操作的格式说明符与操作数类型/数量的兼容性。
 */
#ifndef TC_ANALYZE_6E_H
#define TC_ANALYZE_6E_H

#include "tc_analyzer_internal.h"

/**
 * 6e: 检查 write/read 语句的格式说明符与操作符类型是否兼容。
 *
 * 返回 0 兼容，-1 不兼容（已通过 diag 设置错误）
 */
int tc_check_io_format(TcTypeTag type, const TcFormatFullSpec *spec, int line,
                       TcDiagnostic *diag);

#endif /* TC_ANALYZE_6E_H */

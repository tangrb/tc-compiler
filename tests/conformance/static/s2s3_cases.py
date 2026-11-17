#!/usr/bin/env python3
"""S2/S3 conformance case table for TC 0.0.46.

Expectations are derived from the standard and design, never from compiler
output.  `@HERE@` marks the primary diagnostic position; `@REL@` marks a
related position (same file or another module).  The generator removes the
markers and computes 1-based line and Unicode-scalar columns from the source
text before removal.

Run:
    python3 tests/conformance/static/s2s3_cases.py --write
    python3 tests/conformance/static/s2s3_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "@HERE@"
REL = "@REL@"


def _yaml_scalar(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r")
    return f'"{escaped}"'


def _marker_position(text, marker):
    other = REL if marker == MARK else MARK
    for lineno, line in enumerate(text.splitlines(), 1):
        index = line.find(marker)
        if index < 0:
            continue
        prefix = line[:index]
        removed = prefix.count(MARK) * len(MARK) + prefix.count(REL) * len(REL)
        return lineno, index - removed + 1
    return None


def _strip_markers(text):
    return text.replace(MARK, "").replace(REL, "")


def _compile_yaml(case, primary_file, primary, related):
    lines = [
        f"id: {case['id']}",
        f"profile: {case.get('profile', 'common')}",
        f"source: {case['source']}",
        f"spec: {_yaml_scalar(case['spec'])}",
        "expect:",
        f"  stage: {case['stage']}",
        f"  code: {case['code']}",
        "  primary: {file: %s, line: %d, column: %d}"
        % (primary_file, primary[0], primary[1]),
    ]
    if related:
        lines.append(
            "  related: [{file: %s, line: %d, column: %d}]"
            % (related[0], related[1], related[2])
        )
    else:
        lines.append("  related: []")
    lines.append("  diagnostics: exactly-one")
    return "\n".join(lines) + "\n"


def _tool_yaml(case, profile, argv):
    args = ", ".join(f'"{item}"' for item in argv)
    roots = ""
    if case.get("include_roots"):
        roots = "include_roots: [%s]\n" % ", ".join(
            '"%s"' % item for item in case["include_roots"]
        )
    return (
        f"id: {case['id']}\n"
        f"profile: {profile}\n"
        f"source: {case['source']}\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        f"{roots}"
        "kind: tool\n"
        "tool:\n"
        f"  argv: [{args}]\n"
        "  repeat: 2\n"
        "expect:\n"
        "  exit: 0\n"
        '  stdout_hex: ""\n'
        '  stderr_hex: ""\n'
    )


# --------------------------------------------------------------------------
# Single-file SEM cases (root is #program unless the case imports a library).

SEM_CASES = [
    (
        "TC-SEM-NAME-0001", "name_undefined.tc", "common",
        "§5.6 裸值名先查局部，未命中且非模块成员 -> UNDEFINED_NAME；§12.5.1 未解析标识符",
        "#program\nvar x: int32 = copy int32, @HERE@y\n",
        "SEM", "TC_CE_UNDEFINED_NAME", [],
    ),
    (
        "TC-SEM-NAME-0002", "name_duplicate_local.tc", "common",
        "§5.1 同一函数/程序值名字空间不得重名 -> DUPLICATE_NAME；§12.5.1 第二次声明并关联首次",
        "#program\nvar @REL@x: int32 = copy int32, 1\nvar @HERE@x: int32 = copy int32, 2\n",
        "SEM", "TC_CE_DUPLICATE_NAME", [],
    ),
    (
        "TC-SEM-NAME-0003", "name_self_reference.tc", "common",
        "§5.2 var 初始化器不在声明名字作用域内，自引用 -> UNDEFINED_NAME",
        "#program\nvar x: int32 = copy int32, @HERE@x\n",
        "SEM", "TC_CE_UNDEFINED_NAME", [],
    ),
    (
        "TC-SEM-NAME-0004", "name_forward_struct.tc", "common",
        "§3.7 字段类型不得引用尚未定义完毕的其它结构体（即使 ptr<其它>）-> UNDEFINED_NAME",
        "#program\nstruct A then\n    var b: ptr<@HERE@B>\nend\nstruct B then\n    var x: int32\nend\n",
        "SEM", "TC_CE_UNDEFINED_NAME", [],
    ),
    (
        "TC-SEM-NAME-0005", "name_struct_self_value.tc", "common",
        "§3.7 结构体值字段自引用 -> STRUCT_SELF_VALUE；§12.5.1 自引用字段类型 Token",
        "#program\nstruct S then\n    var x: @HERE@S\nend\n",
        "SEM", "TC_CE_STRUCT_SELF_VALUE", [],
    ),
    (
        "TC-SEM-NAME-0006", "name_empty_struct.tc", "common",
        "§3.7 空结构体 -> EMPTY_STRUCT；§12.5.1 结构体名",
        "#program\nstruct @HERE@E then\nend\n",
        "SEM", "TC_CE_EMPTY_STRUCT", [],
    ),
    (
        "TC-SEM-NAME-0007", "name_sizeof_undefined.tc", "common",
        "§6.13 sizeof 类型参数完全未解析 -> UNDEFINED_NAME",
        "#program\nvar n: usize = sizeof @HERE@T\n",
        "SEM", "TC_CE_UNDEFINED_NAME", [],
    ),
    (
        "TC-SEM-NAME-0008", "name_sizeof_value.tc", "common",
        "§6.13 sizeof 的类型参数解析到已知值名而非类型 -> TYPE_MISMATCH",
        "#program\nvar v: int32 = copy int32, 1\nvar n: usize = sizeof @HERE@v\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0001", "type_copy_mismatch.tc", "common",
        "§3.9/§6.12 赋值/复制严格同型，无隐式加宽 -> TYPE_MISMATCH；§12.5.1 RHS 首 Token",
        "#program\nvar x: int32 = @HERE@copy int64, 1\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0002", "type_ptr_arith.tc", "common",
        "§3.13.2/§6.15.2 ptr<T> 进入算术/位运算 -> TYPE_MISMATCH；首个不匹配操作数",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nvar x: int32 = add int32, @HERE@p, p\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0003", "type_cast_pointer.tc", "common",
        "§3.11 cast 不作用于指针 -> TYPE_MISMATCH（源操作数）",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nvar x: int32 = cast int32, @HERE@p\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0004", "type_bitcast_category.tc", "common",
        "§3.11 bitcast 类别矩阵：浮点 -> 指针禁止，先于位宽 -> TYPE_MISMATCH",
        "#program\nvar f: float64 = copy float64, 1.0\nvar p: ptr<int32> = bitcast ptr<int32>, @HERE@f\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0005", "type_struct_field_void.tc", "common",
        "§3.8 void 不得作结构体字段 -> VOID；§12.5.1 void Token",
        "#program\nstruct S then\n    var x: @HERE@void\nend\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-FIELD-0001", "field_unknown.tc", "common",
        "§6.14 字段访问未知字段 -> UNKNOWN_FIELD；§12.5.1 字段名",
        "#program\nstruct S then\n    var x: int32\nend\nvar s: S = make S, 1\nvar y: int32 = copy int32, s.@HERE@nope\n",
        "SEM", "TC_CE_UNKNOWN_FIELD", [],
    ),
    (
        "TC-SEM-FIELD-0002", "field_intermediate.tc", "common",
        "§6.14 字段路径中间层必须是结构体 -> TYPE_MISMATCH；定位于该字段名",
        "#program\nstruct S then\n    var x: int32\nend\nvar s: S = make S, 1\nvar y: int32 = copy int32, s.x.@HERE@y\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-FIELD-0003", "field_readonly_assign.tc", "common",
        "§6.14 字段赋值的路径上任一 readonly 字段 -> IMMUTABLE_FIELD；§12.5.1 首个 readonly 字段",
        "#program\nstruct S then\n    readonly x: int32\n    var y: int32\nend\nvar s: S = make S, 1, 2\ns.@HERE@x = copy int32, 3\n",
        "SEM", "TC_CE_IMMUTABLE_FIELD", [],
    ),
    (
        "TC-SEM-FIELD-0004", "field_make_missing.tc", "common",
        "§6.2.7 make 缺字段 -> MISSING_FIELD；§12.5.1 make 结构体名",
        "#program\nstruct S then\n    var x: int32\n    var y: int32\nend\nvar s: S = make @HERE@S, 1\n",
        "SEM", "TC_CE_MISSING_FIELD", [],
    ),
    (
        "TC-SEM-FIELD-0005", "field_make_duplicate.tc", "common",
        "§6.2.7 make 重复字段 -> DUPLICATE_FIELD；§12.5.1 第二字段名并关联首次",
        "#program\nstruct S then\n    var x: int32\n    var y: int32\nend\nvar s: S = make S, @REL@x: 1, @HERE@x: 2\n",
        "SEM", "TC_CE_DUPLICATE_FIELD", [],
    ),
    (
        "TC-SEM-FIELD-0006", "field_make_unknown.tc", "common",
        "§6.2.7 make 未知字段 -> UNKNOWN_FIELD；§12.5.1 字段名",
        "#program\nstruct S then\n    var x: int32\nend\nvar s: S = make S, @HERE@z: 1\n",
        "SEM", "TC_CE_UNKNOWN_FIELD", [],
    ),
    (
        "TC-SEM-FIELD-0007", "field_make_order.tc", "common",
        "§6.2.7 make 命名字段顺序不符 -> FIELD_ORDER；§12.5.1 首个顺序不符的字段名",
        "#program\nstruct S then\n    var x: int32\n    var y: int32\nend\nvar s: S = make S, @HERE@y: 2, x: 1\n",
        "SEM", "TC_CE_FIELD_ORDER", [],
    ),
    (
        "TC-SEM-FIELD-0008", "field_make_mixed.tc", "common",
        "§6.2.7 位置字段与命名字段不得混用 -> FIELD_ORDER；定位首个带名字段",
        "#program\nstruct S then\n    var x: int32\n    var y: int32\nend\nvar s: S = make S, 1, @HERE@y: 2\n",
        "SEM", "TC_CE_FIELD_ORDER", [],
    ),
    (
        "TC-SEM-FIELD-0009", "field_make_extra.tc", "common",
        "§6.2.7 make 多余位置字段 -> OPERAND_COUNT；§12.5.1 指令助记符",
        "#program\nstruct S then\n    var x: int32\nend\nvar s: S = @HERE@make S, 1, 2\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-FIELD-0010", "field_struct_duplicate.tc", "common",
        "§3.7 结构体内字段名唯一 -> DUPLICATE_FIELD；关联首次声明",
        "#program\nstruct S then\n    var @REL@x: int32\n    var @HERE@x: int32\nend\n",
        "SEM", "TC_CE_DUPLICATE_FIELD", [],
    ),
    (
        "TC-SEM-ASSIGN-0001", "assign_const.tc", "common",
        "§6.14 对 const 赋值 -> CONST_ASSIGNMENT；§12.5.1 赋值目标名称",
        "#program\nconst K: int32 = 1\n@HERE@K = copy int32, 2\n",
        "SEM", "TC_CE_CONST_ASSIGNMENT", [],
    ),
    (
        "TC-SEM-ASSIGN-0002", "assign_addr_const.tc", "common",
        "§5.5/§9.5 const 不可取址 -> ADDR_OF_CONST；§12.5.1 addr 的 const 目标名",
        "#program\nconst K: int32 = 1\nvar p: ptr<int32> = addr int32, @HERE@K\n",
        "SEM", "TC_CE_ADDR_OF_CONST", [],
    ),
    (
        "TC-SEM-LIT-0001", "lit_unsigned_to_signed.tc", "common",
        "§2.4 u/U 只允许无符号 T -> LITERAL_TYPE；§12.5.1 完整字面量首 Token",
        "#program\nvar x: int32 = copy int32, @HERE@1u\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0002", "lit_negative_to_unsigned.tc", "common",
        "§2.4 负值用于无符号 T -> LITERAL_TYPE",
        "#program\nvar x: uint32 = copy uint32, @HERE@-1\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0003", "lit_float_to_int.tc", "common",
        "§2.4/§6.6.3 浮点字面量不能用于整数上下文 -> LITERAL_TYPE",
        "#program\nvar x: int32 = copy int32, @HERE@1.0\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0004", "lit_bool_to_int.tc", "common",
        "§2.4 true/false 只用于 bool -> LITERAL_TYPE",
        "#program\nvar x: int32 = copy int32, @HERE@true\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0005", "lit_nullptr_to_int.tc", "common",
        "§2.4 nullptr 只用于 ptr<T>/fnptr<...> -> LITERAL_TYPE",
        "#program\nvar x: int32 = copy int32, @HERE@nullptr\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0006", "lit_inf_to_int.tc", "common",
        "§2.4 inf/nan 只能用于浮点上下文 -> LITERAL_TYPE",
        "#program\nvar x: int32 = copy int32, @HERE@inf\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0007", "lit_int8_range.tc", "common",
        "§2.4/§3.13 超出期望类型范围 -> LITERAL_RANGE；§12.5.1 完整字面量",
        "#program\nvar x: int8 = copy int8, @HERE@128\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
    (
        "TC-SEM-LIT-0008", "lit_int8_neg_range.tc", "common",
        "§2.4 负值边界外 -> LITERAL_RANGE；负号属于字面量时定位到 -",
        "#program\nvar x: int8 = copy int8, @HERE@-129\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
    (
        "TC-SEM-MODE-0001", "mode_wrap_unsigned.tc", "common",
        "§6.3 无符号整数固定模 2^n，无 .wrap -> MODE；§12.5.1 模式后缀 Token",
        "#program\nvar x: uint32 = add.@HERE@wrap uint32, 1, 2\n",
        "SEM", "TC_CE_MODE", [],
    ),
    (
        "TC-SEM-MODE-0002", "mode_ieee_int.tc", "common",
        "§6.3 .ieee 只适用于浮点 -> MODE",
        "#program\nvar x: int32 = add.@HERE@ieee int32, 1, 2\n",
        "SEM", "TC_CE_MODE", [],
    ),
    (
        "TC-SEM-MODE-0003", "mode_nsw_not_fast.tc", "common",
        "§3.13.2/§6.15.1 .nsw 在合法 base 但不在 @fast -> ANNOTATION",
        "#program\nvar x: int32 = add.@HERE@nsw int32, 1, 2\n",
        "SEM", "TC_CE_ANNOTATION", [],
    ),
    (
        "TC-SEM-OP-0001", "op_missing_operand.tc", "common",
        "§6.15.1 opcode 已识别但操作数数量不符 -> OPERAND_COUNT；§12.5.1 助记符",
        "#program\nvar x: int32 = @HERE@add int32, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0002", "op_result_discard.tc", "common",
        "§6.15.1 无结果指令的 LHS 必须是 _ -> OPERAND_COUNT",
        "#program\nvar x: int32 = copy int32, 0\nvar p: ptr<int32> = addr int32, x\nunsafe then\n    x = @HERE@store int32, p, 1\nend\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-VOID-0001", "void_var.tc", "common",
        "§3.8 void 不得作 var 类型 -> VOID；§12.5.1 void Token",
        "#program\nvar x: @HERE@void = copy int32, 1\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-VOID-0002", "void_ptr.tc", "common",
        "§3.5/§3.8 ptr<void> 非法 -> VOID；§12.5.1 void Token",
        "#program\nvar p: ptr<@HERE@void> = copy ptr<int32>, nullptr\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-VOID-0003", "void_sizeof.tc", "common",
        "§6.13 sizeof void -> VOID；§12.5.1 void Token",
        "#program\nvar n: usize = sizeof @HERE@void\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-VOID-0004", "void_make.tc", "common",
        "§3.8 void 不是值类型，不能作为 make 的结构体类型 -> VOID",
        "#program\nvar x: int32 = make @HERE@void, 1\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-VOID-0005", "void_arith_type.tc", "common",
        "§3.8/§6.15.2 void 不得作运算类型 -> VOID；§12.5.1 void Token",
        "#program\nvar x: int32 = add @HERE@void, 1, 2\n",
        "SEM", "TC_CE_VOID", [],
    ),
    (
        "TC-SEM-NAME-0009", "name_sizeof_label.tc", "common",
        "§6.13 sizeof 解析到已知标签名而非类型 -> TYPE_MISMATCH",
        "#program\nvar n: usize = sizeof @HERE@L\nlabel L:\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0025", "conv_bitcast_inf_bool.tc", "common",
        "§3.11 bitcast 类别矩阵先于位宽：浮点 -> bool 禁止 -> TYPE_MISMATCH",
        "#program\nvar b: bool = bitcast bool, @HERE@inf\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0026", "conv_bitcast_inf_int8.tc", "common",
        "§6.6.3/§3.11 inf 源类型按目标位宽取 float32/64；int8 不等宽 -> BITCAST_WIDTH",
        "#program\nvar x: int8 = bitcast int8, @HERE@inf\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-LIT-0016", "lit_float64_to_float32.tc", "common",
        "§2.4 浮点字面量后缀决定源类型；f 后缀 -> float32，用于 float64 -> LITERAL_TYPE",
        "#program\nvar x: float64 = copy float64, @HERE@1.0f\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0017", "lit_float32_no_suffix.tc", "common",
        "§2.4 无后缀浮点 -> float64，用于 float32 -> LITERAL_TYPE",
        "#program\nvar x: float32 = copy float32, @HERE@1.0\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0018", "lit_int_to_float.tc", "common",
        "§2.4 整数字面量不能用于浮点上下文（无隐式转换）-> LITERAL_TYPE",
        "#program\nvar x: float64 = copy float64, @HERE@1\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0019", "lit_switch_float.tc", "common",
        "§6.10 switch 选择字面量必须为整数 -> LITERAL_TYPE",
        "#program\n_ = switch @HERE@1.0, L0\nlabel L0:\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-CONV-0037", "conv_bitcast_int_width.tc", "common",
        "§3.11 整数 -> 整数类别允许但位宽不等 -> BITCAST_WIDTH",
        "#program\nvar a: int64 = copy int64, 0\nvar b: int32 = bitcast int32, @HERE@a\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0038", "conv_bitcast_float_width.tc", "common",
        "§3.11 浮点 -> 浮点类别允许但位宽不等 -> BITCAST_WIDTH",
        "#program\nvar a: float64 = copy float64, 1.0\nvar b: float32 = bitcast float32, @HERE@a\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-TYPE-0105", "type_ptr_mismatch.tc", "common",
        "§3.9/§3.11 ptr<T> 按 T 等价，不同 T 无隐式转换 -> TYPE_MISMATCH",
        "#program\nvar a: ptr<int32> = copy ptr<int32>, nullptr\nvar b: ptr<int64> = copy ptr<int64>, @HERE@a\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0106", "type_fnptr_mismatch.tc", "common",
        "§3.9/§3.11 fnptr 签名逐项等价，不同签名无隐式转换 -> TYPE_MISMATCH",
        "#program\nvar a: fnptr<void> = copy fnptr<void>, nullptr\nvar b: fnptr<int32> = copy fnptr<int32>, @HERE@a\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-FIELD-0012", "field_nested_readonly.tc", "common",
        "§6.14 字段赋值路径任一 readonly -> IMMUTABLE_FIELD（嵌套路径）",
        "#program\nstruct Inner then\n    var z: int32\nend\nstruct Outer then\n    readonly ro: Inner\nend\nvar i: Inner = make Inner, 1\nvar o: Outer = make Outer, i\no.@HERE@ro.z = copy int32, 2\n",
        "SEM", "TC_CE_IMMUTABLE_FIELD", [],
    ),
    (
        "TC-SEM-LIT-0009", "lit_br_if_condition.tc", "common",
        "§6.10/§12.2 #9 br_if 条件字面量按 bool 检查 -> LITERAL_TYPE",
        "#program\n_ = br_if @HERE@1, L\nlabel L:\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0010", "lit_switch_selector.tc", "common",
        "§6.10/§12.2 #9 switch 选择字面量按整数检查 -> LITERAL_TYPE",
        "#program\n_ = switch @HERE@true, L0\nlabel L0:\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0011", "lit_cast_source_range.tc", "common",
        "§6.6.3 cast 无后缀整数源类型 int64；超出 int64 -> LITERAL_RANGE",
        "#program\nvar x: int8 = cast int8, @HERE@18446744073709551615\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
    (
        "TC-SEM-LIT-0012", "lit_bitcast_range.tc", "common",
        "§6.6.3 bitcast 整数源位宽 = 目标位宽；无后缀按 signed，值超出源类型 -> LITERAL_RANGE",
        "#program\nvar x: int32 = bitcast int32, @HERE@4294967295\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
    (
        "TC-SEM-LIT-0013", "lit_trunc_float.tc", "common",
        "§6.6.3 trunc 不接受浮点字面量 -> LITERAL_TYPE",
        "#program\nvar x: int32 = trunc int32, @HERE@1.0\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0014", "lit_bitcast_nullptr.tc", "common",
        "§3.11 nullptr 禁止作为 bitcast 源或目标 -> LITERAL_TYPE",
        "#program\nvar x: int64 = bitcast int64, @HERE@nullptr\n",
        "SEM", "TC_CE_LITERAL_TYPE", [],
    ),
    (
        "TC-SEM-LIT-0015", "lit_read_ptr.tc", "common",
        "§6.2.9 read 的 T 只支持标量 -> TYPE_MISMATCH；§12.5.1 类型参数",
        "#program\nvar p: ptr<int32> = read @HERE@ptr<int32>\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0020", "conv_bitcast_int_bool.tc", "common",
        "§3.11 bitcast 类别矩阵：整数 -> bool 禁止，先于位宽 -> TYPE_MISMATCH",
        "#program\nvar b: bool = bitcast bool, @HERE@1\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0021", "conv_bitcast_float_bool.tc", "common",
        "§3.11 bitcast 类别矩阵：浮点 -> bool 禁止 -> TYPE_MISMATCH",
        "#program\nvar f: float64 = copy float64, 1.0\nvar b: bool = bitcast bool, @HERE@f\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0022", "conv_bitcast_ptr_fnptr.tc", "common",
        "§3.11 bitcast 类别矩阵：ptr -> fnptr 禁止 -> TYPE_MISMATCH",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nvar f: fnptr<void> = bitcast fnptr<void>, @HERE@p\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0023", "conv_bitcast_struct.tc", "common",
        "§3.11 bitcast 类别矩阵：结构体不参与 bitcast -> TYPE_MISMATCH",
        "#program\nstruct S then\n    var x: int32\nend\nvar s: S = make S, 1\nvar x: int32 = bitcast S, @HERE@s\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-CONV-0024", "conv_bitcast_bool_source.tc", "common",
        "§3.11 bitcast 类别矩阵：bool 源禁止 -> TYPE_MISMATCH",
        "#program\nvar b: bool = copy bool, true\nvar x: int8 = bitcast int8, @HERE@b\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-OP-0004", "op_var_init_store.tc", "common",
        "§6.15.1 无结果指令不能作 var 初始化器（没有合法的 _ LHS）-> OPERAND_COUNT",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nunsafe then\n    var t: int32 = @HERE@store int32, p, 1\nend\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0005", "op_var_init_trap.tc", "common",
        "§6.15.1 trap 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@trap\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0006", "op_var_init_br.tc", "common",
        "§6.15.1 br 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@br L\nlabel L:\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0007", "op_var_init_br_if.tc", "common",
        "§6.15.1 br_if 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@br_if true, L\nlabel L:\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0008", "op_var_init_switch.tc", "common",
        "§6.15.1 switch 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@switch 0, L\nlabel L:\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0009", "op_var_init_memmove.tc", "common",
        "§6.15.1 memmove 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nunsafe then\n    var t: int32 = @HERE@memmove int32, p, p, 1\nend\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0010", "op_var_init_write.tc", "common",
        "§6.15.1 write 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@write int32, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0011", "op_var_init_writeln.tc", "common",
        "§6.15.1 writeln 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar t: int32 = @HERE@writeln int32, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0012", "op_var_init_store_volatile.tc", "common",
        "§6.15.1 store.volatile 无结果，不能作 var 初始化器 -> OPERAND_COUNT",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nunsafe then\n    var t: int32 = @HERE@store.volatile int32, p, 1\nend\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0013", "op_named_cast_source.tc", "common",
        "§6.1/§6.15.1 命名操作数只允许 call 实参与 make 字段；cast 源命名 -> OPERAND_COUNT（助记符）",
        "#program\nvar x: int64 = @HERE@cast int64, foo: 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0014", "op_named_sizeof_type.tc", "common",
        "§6.1/§6.15.1 命名操作数只允许 call/make；sizeof 类型参数命名 -> OPERAND_COUNT（助记符）",
        "#program\nstruct S then\n    var a: int32\nend\nvar n: usize = @HERE@sizeof foo: S\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0015", "op_named_copy_value.tc", "common",
        "§6.1/§6.15.1 copy 值操作数不得命名 -> OPERAND_COUNT（助记符）",
        "#program\nvar x: int32 = @HERE@copy int32, foo: 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0016", "op_named_br_label.tc", "common",
        "§6.1/§6.15.1 分支标签不得命名 -> OPERAND_COUNT（助记符）",
        "#program\n_ = @HERE@br zz: L\nlabel L:\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0017", "op_named_make_type.tc", "common",
        "§6.1/§6.15.1 make 的结构体类型参数不得命名 -> OPERAND_COUNT（助记符）",
        "#program\nstruct S then\n    var a: int32\nend\nvar s: S = @HERE@make foo: S, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0018", "op_cast_literal_type.tc", "common",
        "§6.15.1 cast 的类型参数位置放值字面量 -> OPERAND_COUNT（助记符）",
        "#program\nvar x: int64 = @HERE@cast 1, 2\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0019", "op_sizeof_literal_type.tc", "common",
        "§6.15.1 sizeof 的类型参数位置放值字面量 -> OPERAND_COUNT（助记符）",
        "#program\nvar n: usize = @HERE@sizeof 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0020", "op_copy_type_as_value.tc", "common",
        "§6.15.1 copy 的值操作数位置放类型记号 -> OPERAND_COUNT（助记符）",
        "#program\nvar x: int32 = @HERE@copy int32, int32\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0021", "op_br_literal_label.tc", "common",
        "§6.15.1 分支标签操作数不是标签记号 -> OPERAND_COUNT（助记符）",
        "#program\n_ = @HERE@br 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0022", "op_switch_literal_label.tc", "common",
        "§6.15.1 switch 标签操作数不是标签记号 -> OPERAND_COUNT（助记符）",
        "#program\n_ = @HERE@switch 0, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0023", "op_addr_literal_target.tc", "common",
        "§6.15.1 addr 目标必须是可寻址名字 -> OPERAND_COUNT（助记符）",
        "#program\nvar p: ptr<int32> = @HERE@addr int32, 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0024", "op_named_funcref.tc", "common",
        "§6.1/§6.15.1 funcaddr 函数操作数不得命名 -> OPERAND_COUNT（助记符）",
        "#program\nvar p: fnptr<void> = @HERE@funcaddr foo: f\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0025", "op_make_mixed_missing.tc", "common",
        "§6.2.7/§12.2 #10 make 混用位置/命名字段仍先检查缺失 -> MISSING_FIELD",
        "#program\nstruct S3 then\n    var a: int32\n    var b: int32\n    var c: int32\nend\nvar s: S3 = make @HERE@S3, 1, b: 2\n",
        "SEM", "TC_CE_MISSING_FIELD", [],
    ),
    (
        "TC-SEM-OP-0026", "op_make_mixed_unknown.tc", "common",
        "§6.2.7/§12.2 #10 make 混用仍先检查未知字段 -> UNKNOWN_FIELD",
        "#program\nstruct S3 then\n    var a: int32\n    var b: int32\n    var c: int32\nend\nvar s: S3 = make S3, 1, @HERE@z: 2\n",
        "SEM", "TC_CE_UNKNOWN_FIELD", [],
    ),
    (
        "TC-SEM-OP-0027", "op_make_mixed_duplicate.tc", "common",
        "§6.2.7/§12.2 #10 make 混用仍先检查重复字段 -> DUPLICATE_FIELD",
        "#program\nstruct S3 then\n    var a: int32\n    var b: int32\n    var c: int32\nend\nvar s: S3 = make S3, @REL@a: 1, @HERE@a: 2, 3\n",
        "SEM", "TC_CE_DUPLICATE_FIELD", [],
    ),
    (
        "TC-SEM-OP-0028", "op_make_mixed_extra.tc", "common",
        "§6.2.7/§12.2 #10 make 多余位置字段 -> OPERAND_COUNT（助记符）",
        "#program\nstruct S3 then\n    var a: int32\n    var b: int32\n    var c: int32\nend\nvar s: S3 = @HERE@make S3, 1, 2, 3, 4, a: 1\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-OP-0003", "op_extra_operand.tc", "common",
        "§6.15.1 操作数数量不符 -> OPERAND_COUNT；§12.5.1 助记符",
        "#program\nvar x: int32 = @HERE@add int32, 1, 2, 3\n",
        "SEM", "TC_CE_OPERAND_COUNT", [],
    ),
    (
        "TC-SEM-TYPE-0101", "type_isize_int32_w32.tc", "w32",
        "§3.9/§13.2.2 32 位 isize 与 int32 同宽但不同型 -> TYPE_MISMATCH",
        "#program\nvar s: isize = copy isize, 0\nvar x: int32 = copy int32, @HERE@s\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0102", "type_usize_uint32_w32.tc", "w32",
        "§3.9/§13.2.2 32 位 usize 与 uint32 同宽但不同型 -> TYPE_MISMATCH",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint32 = copy uint32, @HERE@s\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0103", "type_isize_int64_w64.tc", "w64",
        "§3.9/§13.2.2 64 位 isize 与 int64 同宽但不同型 -> TYPE_MISMATCH",
        "#program\nvar s: isize = copy isize, 0\nvar x: int64 = copy int64, @HERE@s\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-TYPE-0104", "type_usize_uint64_w64.tc", "w64",
        "§3.9/§13.2.2 64 位 usize 与 uint64 同宽但不同型 -> TYPE_MISMATCH",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint64 = copy uint64, @HERE@s\n",
        "SEM", "TC_CE_TYPE_MISMATCH", [],
    ),
    (
        "TC-SEM-FIELD-0011", "field_unknown_vs_readonly.tc", "common",
        "§5.7.3 字段存在性先于 readonly 路径 -> UNKNOWN_FIELD（即使前缀 readonly）",
        "#program\nstruct Inner then\n    var z: int32\nend\nstruct Outer then\n    readonly ro: Inner\n    var w: int32\nend\nvar i: Inner = make Inner, 1\nvar o: Outer = make Outer, i, 2\no.ro.@HERE@nope = copy int32, 1\n",
        "SEM", "TC_CE_UNKNOWN_FIELD", [],
    ),
    (
        "TC-SEM-CONV-0001", "conv_bitcast_width_w64.tc", "common",
        "§3.11 类别允许但位宽不等 -> BITCAST_WIDTH；§13.2.2 bitcast isize<->int32 仅 32 位",
        "#program\nvar s: isize = copy isize, 0\nvar x: int32 = bitcast int32, @HERE@s\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0002", "conv_trunc_width_w64.tc", "common",
        "§3.11 trunc 目标位宽 >= 源 -> TRUNC_WIDTH；§12.5.1 目标类型 Token",
        "#program\nvar s: isize = copy isize, 0\nvar x: isize = trunc @HERE@isize, s\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONST-0001", "const_padding_suffix.tc", "common",
        "§2.6 @padding(N) 必须是非负十进制字面量（无 u/进制/负号）-> CONST_FORM",
        "#program\nstruct S then\n    var x: int32 @padding(@HERE@4u)\nend\n",
        "SEM", "TC_CE_CONST_FORM", [],
    ),
    (
        "TC-SEM-CONST-0002", "const_padding_hex.tc", "common",
        "§2.6 @padding(N) 不得使用进制前缀 -> CONST_FORM",
        "#program\nstruct S then\n    var x: int32 @padding(@HERE@0x4)\nend\n",
        "SEM", "TC_CE_CONST_FORM", [],
    ),
    (
        "TC-SEM-CONST-0003", "const_padding_negative.tc", "common",
        "§2.6 @padding(N) 不得为负 -> CONST_FORM",
        "#program\nstruct S then\n    var x: int32 @padding(@HERE@-1)\nend\n",
        "SEM", "TC_CE_CONST_FORM", [],
    ),
    (
        "TC-SEM-LAYOUT-0001", "layout_overflow_w32.tc", "w32",
        "§3.7/§6.13 布局求和在 W 位 usize 中溢出 -> LAYOUT_OVERFLOW（仅 32 位）",
        "#program\nstruct S then\n    var @HERE@x: uint8 @padding(4294967295)\n    var y: uint8\nend\n",
        "SEM", "TC_CE_LAYOUT_OVERFLOW", [],
    ),
    (
        "TC-SEM-LAYOUT-0002", "layout_overflow_w64.tc", "w64",
        "§3.7/§6.13 布局求和在 W 位 usize 中溢出 -> LAYOUT_OVERFLOW（64 位）",
        "#program\nstruct S then\n    var @HERE@x: uint8 @padding(18446744073709551615)\nend\n",
        "SEM", "TC_CE_LAYOUT_OVERFLOW", [],
    ),
    (
        "TC-SEM-CONV-0003", "conv_bitcast_i64_isize_w32.tc", "w32",
        "§13.2.2 bitcast isize<->int64 在 32 位位宽不等 -> BITCAST_WIDTH",
        "#program\nvar s: isize = copy isize, 0\nvar x: int64 = bitcast int64, @HERE@s\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0004", "conv_bitcast_u32_usize_w64.tc", "w64",
        "§13.2.2 bitcast usize<->uint32 在 64 位位宽不等 -> BITCAST_WIDTH",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint32 = bitcast uint32, @HERE@s\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0005", "conv_bitcast_u64_usize_w32.tc", "w32",
        "§13.2.2 bitcast usize<->uint64 在 32 位位宽不等 -> BITCAST_WIDTH",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint64 = bitcast uint64, @HERE@s\n",
        "SEM", "TC_CE_BITCAST_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0006", "conv_trunc_isize_i64_w64.tc", "w64",
        "§13.2.2 trunc isize,int64 在 64 位报 TRUNC_WIDTH（64 = 64）",
        "#program\nvar x: int64 = copy int64, 0\nvar s: isize = trunc @HERE@isize, x\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0007", "conv_trunc_i32_isize_w32.tc", "w32",
        "§13.2.2 trunc int32,isize 在 32 位报 TRUNC_WIDTH（32 = 32）",
        "#program\nvar s: isize = copy isize, 0\nvar x: int32 = trunc @HERE@int32, s\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0008", "conv_trunc_usize_i64_w64.tc", "w64",
        "§13.2.2 trunc usize,int64 在 64 位报 TRUNC_WIDTH（64 = 64）",
        "#program\nvar x: int64 = copy int64, 0\nvar s: usize = trunc @HERE@usize, x\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0009", "conv_trunc_i32_usize_w32.tc", "w32",
        "§13.2.2 trunc int32,usize 在 32 位报 TRUNC_WIDTH（32 = 32）",
        "#program\nvar s: usize = copy usize, 0\nvar x: int32 = trunc @HERE@int32, s\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0010", "conv_bitcast_w32_ok.tc", "w32",
        "§13.2.2 bitcast isize<->int32 在 32 位等宽合法",
        "#program\nvar s: isize = copy isize, 0\nvar x: int32 = bitcast int32, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0011", "conv_bitcast_w64_ok.tc", "w64",
        "§13.2.2 bitcast isize<->int64 在 64 位等宽合法",
        "#program\nvar s: isize = copy isize, 0\nvar x: int64 = bitcast int64, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0012", "conv_bitcast_usize_w32.tc", "w32",
        "§13.2.2 bitcast usize<->uint32 在 32 位等宽合法",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint32 = bitcast uint32, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0013", "conv_bitcast_usize_w64.tc", "w64",
        "§13.2.2 bitcast usize<->uint64 在 64 位等宽合法",
        "#program\nvar s: usize = copy usize, 0\nvar x: uint64 = bitcast uint64, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0014", "conv_trunc_w32_ok.tc", "w32",
        "§13.2.2 trunc isize,int64 在 32 位合法（32 < 64）",
        "#program\nvar x: int64 = copy int64, 0\nvar s: isize = trunc isize, x\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0015", "conv_trunc_w64_ok.tc", "w64",
        "§13.2.2 trunc int32,isize 在 64 位合法（32 < 64）",
        "#program\nvar s: isize = copy isize, 0\nvar x: int32 = trunc int32, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0016", "conv_trunc_usize_w32.tc", "w32",
        "§13.2.2 trunc usize,int64 在 32 位合法（32 < 64）",
        "#program\nvar x: int64 = copy int64, 0\nvar s: usize = trunc usize, x\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0017", "conv_trunc_int32_usize_w64.tc", "w64",
        "§13.2.2 trunc int32,usize 在 64 位合法（32 < 64）",
        "#program\nvar s: usize = copy usize, 0\nvar x: int32 = trunc int32, s\n",
        None, None, [],
    ),
    (
        "TC-SEM-CONV-0018", "conv_trunc_w64_width.tc", "w64",
        "§13.2.2 trunc isize,int64 在 64 位报 TRUNC_WIDTH（64 = 64）",
        "#program\nvar s: isize = copy isize, 0\nvar x: isize = trunc @HERE@isize, s\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-CONV-0019", "conv_trunc_w32_width.tc", "w32",
        "§13.2.2 trunc int32,usize 在 32 位报 TRUNC_WIDTH（32 = 32）",
        "#program\nvar s: usize = copy usize, 0\nvar x: int32 = trunc @HERE@int32, s\n",
        "SEM", "TC_CE_TRUNC_WIDTH", [],
    ),
    (
        "TC-SEM-LIT-0100", "lit_isize_w32_range.tc", "w32",
        "§13.2.2 isize 字面量范围按 W；32 位下 2^31 不可表示 -> LITERAL_RANGE",
        "#program\nvar x: isize = copy isize, @HERE@2147483648\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
    (
        "TC-SEM-LIT-0101", "lit_usize_w32_range.tc", "w32",
        "§13.2.2 usize 字面量范围按 W；32 位下 2^32 不可表示 -> LITERAL_RANGE",
        "#program\nvar x: usize = copy usize, @HERE@4294967296\n",
        "SEM", "TC_CE_LITERAL_RANGE", [],
    ),
]

def _init_expr(type_name):
    if type_name.startswith("ptr") or type_name.startswith("fnptr"):
        return f"copy {type_name}, nullptr"
    if type_name == "S":
        return "make S, 0"
    if type_name == "bool":
        return "copy bool, false"
    if type_name == "float32":
        return "copy float32, 0.0f"
    if type_name == "float64":
        return "copy float64, 0.0"
    return f"copy {type_name}, 0"


def _bitcast_matrix_cases():
    """One case per cell of the standard §3.11 category matrix.

    Allowed cells are acceptance tool cases with equal-width types under W=64;
    forbidden cells are compile cases expecting TYPE_MISMATCH at the source
    operand (category is checked before width)."""
    cells = [
        ("int", "int", "int32", "int32", True),
        ("int", "float", "int32", "float32", True),
        ("int", "bool", "int32", "bool", False),
        ("int", "ptr", "int64", "ptr<int32>", True),
        ("int", "fnptr", "int64", "fnptr<void>", True),
        ("int", "struct", "int32", "S", False),
        ("float", "int", "float32", "int32", True),
        ("float", "float", "float32", "float32", True),
        ("float", "bool", "float32", "bool", False),
        ("float", "ptr", "float32", "ptr<int32>", False),
        ("float", "fnptr", "float32", "fnptr<void>", False),
        ("float", "struct", "float32", "S", False),
        ("bool", "int", "bool", "int32", False),
        ("bool", "float", "bool", "float32", False),
        ("bool", "bool", "bool", "bool", False),
        ("bool", "ptr", "bool", "ptr<int32>", False),
        ("bool", "fnptr", "bool", "fnptr<void>", False),
        ("bool", "struct", "bool", "S", False),
        ("ptr", "int", "ptr<int32>", "int64", True),
        ("ptr", "float", "ptr<int32>", "float32", False),
        ("ptr", "bool", "ptr<int32>", "bool", False),
        ("ptr", "ptr", "ptr<int32>", "ptr<float64>", True),
        ("ptr", "fnptr", "ptr<int32>", "fnptr<void>", False),
        ("ptr", "struct", "ptr<int32>", "S", False),
        ("fnptr", "int", "fnptr<void>", "int64", True),
        ("fnptr", "float", "fnptr<void>", "float32", False),
        ("fnptr", "bool", "fnptr<void>", "bool", False),
        ("fnptr", "ptr", "fnptr<void>", "ptr<int32>", False),
        ("fnptr", "fnptr", "fnptr<void>", "fnptr<void>", True),
        ("fnptr", "struct", "fnptr<void>", "S", False),
        ("struct", "int", "S", "int32", False),
        ("struct", "float", "S", "float32", False),
        ("struct", "bool", "S", "bool", False),
        ("struct", "ptr", "S", "ptr<int32>", False),
        ("struct", "fnptr", "S", "fnptr<void>", False),
        ("struct", "struct", "S", "S", False),
    ]
    cases = []
    for index, (src_cat, dst_cat, src_type, dst_type, allowed) in enumerate(
            cells, 1):
        marker = "" if allowed else "@HERE@"
        unsafe = allowed and (
            (src_cat == "int" and dst_cat in ("ptr", "fnptr"))
            or (src_cat in ("ptr", "fnptr") and dst_cat == "int")
        )
        cast = f"bitcast {dst_type}, {marker}src"
        if unsafe:
            body = f"unsafe then\n    var dst: {dst_type} = {cast}\nend\n"
        else:
            body = f"var dst: {dst_type} = {cast}\n"
        text = (
            "#program\n"
            "struct S then\n"
            "    var x: int32\n"
            "end\n"
            f"var src: {src_type} = {_init_expr(src_type)}\n"
            + body
        )
        case = (
            f"TC-SEM-BITCAST-{index:04d}",
            f"bitcast_{src_cat}_{dst_cat}.tc",
            "w64",
            f"§3.11 bitcast 类别矩阵：{src_cat} -> {dst_cat} "
            + ("允许且等宽" if allowed else "禁止 -> TYPE_MISMATCH"),
            text,
            None if allowed else "SEM",
            None if allowed else "TC_CE_TYPE_MISMATCH",
            [],
        )
        cases.append(case)
    return cases


SEM_CASES += _bitcast_matrix_cases()

# Cases with a root program and library fixtures.
IMPORT_CASES = [
    {
        "id": "TC-IMP-0001", "spec": "§4.5 找不到目标文件 -> IMPORT_NOT_FOUND；位置为 import 模块名",
        "stage": "IMPORT", "code": "TC_CE_IMPORT_NOT_FOUND", "profile": "common",
        "files": {"root.tc": "#program\nimport @HERE@missing\n"},
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0002", "spec": "§4.5/§4.11.2 导入目标不是 #lib -> IMPORT_NOT_LIB；目标仍 LEX/SYN",
        "stage": "IMPORT", "code": "TC_CE_IMPORT_NOT_LIB", "profile": "common",
        "files": {
            "root.tc": "#program\nimport @HERE@prog\n",
            "prog.tc": "#program\nvar x: int32 = copy int32, 1\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0003", "spec": "§4.5 同一导入者重复引用同一解析目标 -> DUPLICATE_IMPORT；关联首次导入",
        "stage": "IMPORT", "code": "TC_CE_DUPLICATE_IMPORT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport @REL@a\nimport @HERE@a\n",
            "a.tc": "#lib\npublic func f() int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0004", "spec": "§4.5 目标在当前递归栈上 -> CIRCULAR_IMPORT（循环优先于重复）；最早 import 边",
        "stage": "IMPORT", "code": "TC_CE_CIRCULAR_IMPORT", "profile": "common",
        "files": {
            "root.tc": "@REL@#program\nimport @HERE@a\n",
            "a.tc": "#lib\nimport root\npublic func f() int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0005", "spec": "§4.5 导入名占用模块命名空间，与本地名冲突 -> IMPORT_NAME_CONFLICT；关联冲突声明",
        "stage": "IMPORT", "code": "TC_CE_IMPORT_NAME_CONFLICT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport @HERE@a\nvar @REL@a: int32 = copy int32, 1\n",
            "a.tc": "#lib\npublic func f() int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0101", "spec": "§5.6 值位置裸名命中本模块值成员 -> FUNCTION_SCOPE_ACCESS",
        "stage": "SEM", "code": "TC_CE_FUNCTION_SCOPE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    _ = return 0\nend\npublic func g() int32 then\n    var x: int32 = copy int32, @HERE@f\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0102", "spec": "§4.8 模块成员命名空间重名 -> DUPLICATE_MEMBER；关联首次声明",
        "stage": "SEM", "code": "TC_CE_DUPLICATE_MEMBER", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic struct @REL@S then\n    var x: int32\nend\npublic const @HERE@S: int32 = 1\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0103", "spec": "§8.2 形参名在签名内唯一 -> DUPLICATE_PARAMETER；关联首次",
        "stage": "SEM", "code": "TC_CE_DUPLICATE_PARAMETER", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f(@REL@a: int32, @HERE@a: int32) int32 then\n    _ = return a\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0104", "spec": "§4.6 访问导入的 private const -> PRIVATE_ACCESS；关联私有声明",
        "stage": "SEM", "code": "TC_CE_PRIVATE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = copy int32, lib.@HERE@K\n",
            "lib.tc": "#lib\nprivate const @REL@K: int32 = 1\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0105", "spec": "§4.6 访问导入的 private 类型（非公开位置）-> PRIVATE_ACCESS",
        "stage": "SEM", "code": "TC_CE_PRIVATE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar p: lib.@HERE@P = copy lib.P, p\n",
            "lib.tc": "#lib\nprivate struct @REL@P then\n    var x: int32\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0106", "spec": "§4.6 private 类型出现在 public 签名 -> PRIVATE_TYPE；关联类型声明",
        "stage": "SEM", "code": "TC_CE_PRIVATE_TYPE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\nprivate struct @REL@P then\n    var x: int32\nend\npublic func f(p: @HERE@P) int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0107", "spec": "§4.6 private 类型出现在 public struct 字段 -> PRIVATE_TYPE",
        "stage": "SEM", "code": "TC_CE_PRIVATE_TYPE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\nprivate struct @REL@P then\n    var x: int32\nend\npublic struct S then\n    var p: @HERE@P\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-ASSIGN-0101", "spec": "§5.3/§6.14 对参数赋值 -> PARAMETER_ASSIGNMENT",
        "stage": "SEM", "code": "TC_CE_PARAMETER_ASSIGNMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f(x: int32) int32 then\n    @HERE@x = copy int32, 2\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-ASSIGN-0102", "spec": "§6.14 对 Self.K 赋值 -> CONST_ASSIGNMENT",
        "stage": "SEM", "code": "TC_CE_CONST_ASSIGNMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic const K: int32 = 1\npublic func f() int32 then\n    Self.@HERE@K = copy int32, 2\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-FUNC-0101", "spec": "§6.15.1 函数操作数形态正确但目标未解析 -> UNDEFINED_FUNCTION；位置函数名",
        "stage": "SEM", "code": "TC_CE_UNDEFINED_FUNCTION", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    var x: int32 = call Self.@HERE@nope\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-FUNC-0102", "spec": "§4.6 调用导入的 private 函数 -> PRIVATE_ACCESS",
        "stage": "SEM", "code": "TC_CE_PRIVATE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = call lib.@HERE@g\n",
            "lib.tc": "#lib\nprivate func @REL@g() int32 then\n    _ = return 1\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-ANN-0001", "spec": "§8.6/§13.2.2 @align 的 N 按 W 位地址宽度检查；32 位下 2^32 不可表示 -> ANNOTATION",
        "stage": "SEM", "code": "TC_CE_ANNOTATION", "profile": "w32",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f(@align(@HERE@4294967296) p: ptr<int32>) int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-ANN-0002", "spec": "§8.6/§13.2.2 @range 的 isize 参数按 W 检查；32 位下 2^32 不可表示 -> LITERAL_RANGE",
        "stage": "SEM", "code": "TC_CE_LITERAL_RANGE", "profile": "w32",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f(@range(0, @HERE@4294967296) n: isize) int32 then\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0008", "spec": "§5.6 裸名命中本模块 static var 作为赋值目标 -> FUNCTION_SCOPE_ACCESS（必须 Self.）",
        "stage": "SEM", "code": "TC_CE_FUNCTION_SCOPE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic static var V: int32 = 2\npublic func bad() int32 then\n    @HERE@V = copy int32, 9\n    _ = return 1\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0112", "spec": "§8.8.2 位置实参与命名实参不得混用 -> ARGUMENT_ORDER；定位首个带名实参",
        "stage": "SEM", "code": "TC_CE_ARGUMENT_ORDER", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = call lib.t3, 1, @HERE@b: 2, c: 3\n",
            "lib.tc": "#lib\npublic func t3(a: int32, b: int32, c: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0102", "spec": "§8.8.2/§12.2 #10 call 混用位置/命名实参仍先检查缺失 -> MISSING_ARGUMENT；§12.5.1 指出声明顺序最靠前的缺失形参",
        "stage": "SEM", "code": "TC_CE_MISSING_ARGUMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = @HERE@call lib.t3, 1, b: 2\n",
            "lib.tc": "#lib\npublic func t3(a: int32, b: int32, @REL@c: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0103", "spec": "§8.8.2/§12.2 #10 call 混用仍先检查未知实参 -> UNKNOWN_ARGUMENT",
        "stage": "SEM", "code": "TC_CE_UNKNOWN_ARGUMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = call lib.t3, 1, @HERE@z: 2\n",
            "lib.tc": "#lib\npublic func t3(a: int32, b: int32, c: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0104", "spec": "§8.8.2/§12.2 #10 call 混用仍先检查重复实参 -> DUPLICATE_ARGUMENT",
        "stage": "SEM", "code": "TC_CE_DUPLICATE_ARGUMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = call lib.f, @REL@a: 1, @HERE@a: 2, 3\n",
            "lib.tc": "#lib\npublic func f(a: int32, b: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0105", "spec": "§8.8.2/§12.2 #10 call 多余位置实参/数量不符 -> ARGUMENT_COUNT（助记符）",
        "stage": "SEM", "code": "TC_CE_ARGUMENT_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = @HERE@call lib.f, 1, 2, 3, a: 4\n",
            "lib.tc": "#lib\npublic func f(a: int32, b: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0106", "spec": "§8.8.2 call 的 void LHS/返回形态先于实参检查 -> VOID",
        "stage": "SEM", "code": "TC_CE_VOID", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar a: int32 = copy int32, 1\na = @HERE@call lib.z, 1\n",
            "lib.tc": "#lib\npublic func z() void then\n    _ = return\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0107", "spec": "§8.8.2 call 目标存在性先于 LHS 解析 -> UNDEFINED_FUNCTION",
        "stage": "SEM", "code": "TC_CE_UNDEFINED_FUNCTION", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nnope = call lib.@HERE@nope, 1, 2\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    _ = return 0\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0108", "spec": "§6.15.1 call 函数操作数必须 Self.f/m.f，命名目标 -> OPERAND_COUNT（助记符）",
        "stage": "SEM", "code": "TC_CE_OPERAND_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar r: int32 = @HERE@call foo: lib.f, 1\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0109", "spec": "§6.15.1 call_indirect 的 fp 操作数不得命名 -> OPERAND_COUNT（助记符）",
        "stage": "SEM", "code": "TC_CE_OPERAND_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar fv: fnptr<int32, int32> = funcaddr lib.f\nvar r: int32 = @HERE@call_indirect foo: fv, 1\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0110", "spec": "§6.2.6/§8.4 call_indirect 只按位置，命名实参 -> UNKNOWN_ARGUMENT",
        "stage": "SEM", "code": "TC_CE_UNKNOWN_ARGUMENT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar fv: fnptr<int32, int32> = funcaddr lib.f\nunsafe then\n    var r: int32 = call_indirect fv, @HERE@x: 1\nend\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0111", "spec": "§6.1/§6.15.1 return 操作数不得命名 -> OPERAND_COUNT（助记符）",
        "stage": "SEM", "code": "TC_CE_OPERAND_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    _ = @HERE@return foo: 1\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-OP-0101", "spec": "§6.15.1 无结果指令在 #lib 函数体作 var 初始化器 -> OPERAND_COUNT",
        "stage": "SEM", "code": "TC_CE_OPERAND_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    var t: int32 = @HERE@trap\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0109", "spec": "§4.11.3 导入 private 非函数成员作 call 目标仍报 PRIVATE_ACCESS，不得降级为 UNDEFINED_FUNCTION",
        "stage": "SEM", "code": "TC_CE_PRIVATE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = call lib.@HERE@P\n",
            "lib.tc": "#lib\nprivate struct @REL@P then\n    var x: int32\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0110", "spec": "§5.6/§6.14 裸名模块 const 作赋值目标 -> FUNCTION_SCOPE_ACCESS（必须 Self.）",
        "stage": "SEM", "code": "TC_CE_FUNCTION_SCOPE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\nprivate const K: int32 = 1\npublic func f() int32 then\n    @HERE@K = copy int32, 2\n    _ = return 0\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0111", "spec": "§14.3/§14.5 类型语法 struct_name=IDENT|IDENT.IDENT；Self 是关键字，Self.S 不能作类型参数 -> OPERAND_COUNT",
        "stage": "SEM", "code": "TC_CE_OPERAND_COUNT", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic struct S then\n    var x: int32\nend\npublic func f() usize then\n    var n: usize = @HERE@sizeof Self.S\n    _ = return n\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0113", "spec": "§4.11.3 导入 private 非类型成员出现在类型位置 -> PRIVATE_ACCESS，不得降级为 TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_PRIVATE_ACCESS", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar n: usize = sizeof lib.@HERE@K\n",
            "lib.tc": "#lib\nprivate const @REL@K: int32 = 1\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0112", "spec": "§5.6/§6.13 限定成员类型解析未命中 -> UNDEFINED_NAME（位置成员名）",
        "stage": "SEM", "code": "TC_CE_UNDEFINED_NAME", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar n: usize = sizeof lib.@HERE@NOPE\n",
            "lib.tc": "#lib\npublic struct S then\n    var x: int32\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-SEM-VOID-0101", "spec": "§6.15.2 void 调用只能写 _；用作值/接收目标 -> VOID",
        "stage": "SEM", "code": "TC_CE_VOID", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = @HERE@call lib.f\n",
            "lib.tc": "#lib\npublic func f() void then\n    _ = return\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-LIT-0111", "spec": "§8.3 return 操作数按返回类型检查；浮点字面量用于 int32 -> LITERAL_TYPE",
        "stage": "SEM", "code": "TC_CE_LITERAL_TYPE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    _ = return @HERE@1.0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-MODE-0102", "spec": "§10.2.1 .nuw 只用于无符号整数，@fast 内 signed -> MODE",
        "stage": "SEM", "code": "TC_CE_MODE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f() int32 then\n    var x: int32 = add.@HERE@nuw int32, 1, 2\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-MODE-0103", "spec": "§10.2.1 .nsw 只用于有符号整数，@fast 内 unsigned -> MODE",
        "stage": "SEM", "code": "TC_CE_MODE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f() uint32 then\n    var x: uint32 = add.@HERE@nsw uint32, 1, 2\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-CONV-0101", "spec": "§3.11 fnptr -> fnptr 仅同签名；不同签名 -> TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_TYPE_MISMATCH", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\npublic func g() int32 then\n    var p: fnptr<int32, int32> = funcaddr Self.f\n    var q: fnptr<int32, int64> = bitcast fnptr<int32, int64>, @HERE@p\n    _ = return 0\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-NAME-0108", "spec": "§4.6 private 类型出现在 public const 初始化器 -> PRIVATE_TYPE",
        "stage": "SEM", "code": "TC_CE_PRIVATE_TYPE", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\nprivate struct @REL@P then\n    var x: int32\nend\npublic const X: int32 = sizeof @HERE@P\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0006", "spec": "§1.8/§12.2 #1 IMPORT 阶段早于 SEM：IMPORT 码优先于被导入模块的 SEM 错误",
        "stage": "IMPORT", "code": "TC_CE_IMPORT_NOT_FOUND", "profile": "common",
        "files": {
            "root.tc": "#program\nimport @HERE@missing\nimport bad\n",
            "bad.tc": "#lib\npublic func f() int32 then\n    var x: int32 = copy int64, 1\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-IMP-0007", "spec": "§1.8/§4.11.1 LEX/SYN 错误立即报告，优先于已记录的 IMPORT 错误",
        "stage": "SYN", "code": "TC_CE_SYNTAX", "profile": "common",
        "files": {
            "root.tc": "#program\nimport missing\nimport badsyn\n",
            "badsyn.tc": "#lib\npublic func f() int32 then\n    var x: int32 = @HERE@1\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-MODE-0101", "spec": "§3.13.2 .nsw 在 @fast 函数内合法；非 @fast -> ANNOTATION（在库函数中）",
        "stage": "SEM", "code": "TC_CE_ANNOTATION", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic func f() int32 then\n    var x: int32 = add.@HERE@nsw int32, 1, 2\n    _ = return x\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-LIT-0113", "spec": "§6.7/§12.2 #9 store 值字面量按期望类型检查 -> LITERAL_TYPE，不报 TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_LITERAL_TYPE", "profile": "common",
        "files": {
            "root.tc": "#program\nvar a: int32 = copy int32, 1\nvar p: ptr<int32> = addr int32, a\nunsafe then\n_ = store int32, p, @HERE@true\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0107", "spec": "§3.11 cast 不作用于指针；fnptr 操作数 -> TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_TYPE_MISMATCH", "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar fv: fnptr<int32, int32> = funcaddr lib.f\nvar x: int32 = cast int32, @HERE@fv\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0108", "spec": "§3.11 cast 不作用于结构体值 -> TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_TYPE_MISMATCH", "profile": "common",
        "files": {
            "root.tc": "#program\nstruct S then\n    var x: int32\nend\nvar s: S = make S, 1\nvar x: int32 = cast int32, @HERE@s\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0109", "spec": "§6.6 trunc 只窄化整数；bool 变量源 -> TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_TYPE_MISMATCH", "profile": "common",
        "files": {
            "root.tc": "#program\nvar b: bool = copy bool, true\nvar x: int8 = trunc int8, @HERE@b\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0110", "spec": "§6.6 trunc 只窄化整数；浮点变量源 -> TYPE_MISMATCH",
        "stage": "SEM", "code": "TC_CE_TYPE_MISMATCH", "profile": "common",
        "files": {
            "root.tc": "#program\nvar f: float64 = copy float64, 1.5\nvar x: int8 = trunc int8, @HERE@f\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0111", "spec": "§6.6.3/§13.2.2 W=64 fnptr(64 位) 与 int32 位宽不等 -> BITCAST_WIDTH",
        "stage": "SEM", "code": "TC_CE_BITCAST_WIDTH", "profile": "w64",
        "files": {
            "root.tc": "#program\nimport lib\nvar fv: fnptr<int32, int32> = funcaddr lib.f\nunsafe then\nvar x: int32 = bitcast int32, @HERE@fv\nend\n",
            "lib.tc": "#lib\npublic func f(a: int32) int32 then\n    _ = return a\nend\n",
        },
        "source": "root.tc",
    },
    {
        "id": "TC-SEM-TYPE-0112", "spec": "§6.6.3/§13.2.2 W=64 ptr(64 位) 与 int32 位宽不等 -> BITCAST_WIDTH",
        "stage": "SEM", "code": "TC_CE_BITCAST_WIDTH", "profile": "w64",
        "files": {
            "root.tc": "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nunsafe then\nvar x: int32 = bitcast int32, @HERE@p\nend\n",
        },
        "source": "root.tc",
    },
]

# Positive acceptance cases exercised through the tool.
TOOL_CASES = [
    {
        "id": "TC-TOOL-SEM-0001", "spec": "§3.7/§6.2.7/§6.13 结构体字段布局、make/copy、字段读写、sizeof 类型参数；静态接受",
        "profile": "common",
        "files": {"root.tc": """#program
struct S then
    var a: int32
    readonly b: uint8 @padding(3)
    var c: float64
end
var s: S = make S, 1, 2, 3.5
var t: S = copy S, s
var x: int32 = copy int32, t.a
var n: usize = sizeof S
_ = write int32, x
_ = write usize, n
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0002", "spec": "§3.11 转换矩阵正例：cast 数值/bool、trunc 窄化、bitcast 等宽数值/指针、ptr<->ptr 重标记",
        "profile": "common",
        "files": {"root.tc": """#program
struct S then
    var x: int32
end
var i: int64 = copy int64, 7
var x: int32 = trunc int32, i
var f: float64 = cast float64, x
var b: bool = cast bool, f
var p: ptr<int32> = copy ptr<int32>, nullptr
var q: ptr<int32> = bitcast ptr<int32>, p
var u: ptr<int32> = bitcast ptr<int32>, q
unsafe then
    var w: int64 = bitcast int64, u
    var v: ptr<int32> = bitcast ptr<int32>, w
end
var bits: uint32 = bitcast uint32, 1065353216
var ff: float32 = bitcast float32, bits
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0003", "spec": "§2.4/§6.6.3 字面量边界与转换源类型：无后缀/负值/进制/u/f/inf/nan/nullptr",
        "profile": "common",
        "files": {"root.tc": """#program
var a: int8 = copy int8, -128
var b: int8 = copy int8, 127
var c: uint8 = copy uint8, 255
var d: int64 = copy int64, 9223372036854775807
var e: uint64 = copy uint64, 18446744073709551615u
var f: float32 = copy float32, 1.5f
var g: float64 = copy float64, inf
var h: float64 = copy float64, -inf
var i: float64 = copy float64, nan
var p: ptr<int32> = copy ptr<int32>, nullptr
var j: int8 = cast int8, 300
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0004", "spec": "§6.3 模式正例：signed .wrap、浮点 .ieee、@fast 内 .nsw/.nuw；§10.2 承诺不在静态拒绝",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nunsafe then\n    var x: int32 = call lib.f\nend\n",
            "lib.tc": """#lib
public @fast func f() int32 then
    var a: int32 = add.nsw int32, 1, 2
    var b: uint32 = add.nuw uint32, 1, 2
    var c: int32 = add.wrap int32, a, 3
    var d: float64 = add.ieee float64, 1.0, 2.0
    var e: float32 = div.ieee float32, 4.0f, 2.0f
    _ = return c
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0005", "spec": "§3.6/§8.5/§6.2.6 fnptr 签名、funcaddr、call_indirect、funceq；参数名不影响等价",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = call lib.f\n",
            "lib.tc": """#lib
public func sum(a: int32, b: int32) int32 then
    _ = return a
end
public func f() int32 then
    var p: fnptr<int32, int32, int32> = funcaddr Self.sum
    var q: fnptr<int32, int32, int32> = copy fnptr<int32, int32, int32>, p
    var same: bool = funceq fnptr<int32, int32, int32>, p, q
    var nullq: fnptr<int32, int32, int32> = copy fnptr<int32, int32, int32>, nullptr
    var eqn: bool = funceq fnptr<int32, int32, int32>, nullq, nullq
    var x: int32 = copy int32, 0
    unsafe then
        x = call_indirect q, 1, 2
        var y: int32 = call_indirect nullq, 1, 2
        _ = write int32, y
    end
    _ = return x
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0006", "spec": "§4.11.1 钻石依赖：module_order root,a,c,b；c 只解析一次；重复复用不报错",
        "profile": "common",
        "files": {
            "root.tc": """#program
import a
import b
var x: int32 = call a.f
var y: int32 = call b.g
""",
            "a.tc": """#lib
import c
public func f() int32 then
    _ = return c.K
end
""",
            "b.tc": """#lib
import c
public func g() int32 then
    _ = return c.K
end
""",
            "c.tc": "#lib\npublic const K: int32 = 7\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0007", "spec": "§4.5 include roots 按顺序搜索；导入文件目录之后首个匹配胜出",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport inc\nvar x: int32 = call inc.f\n",
            "libs/inc.tc": "#lib\npublic func f() int32 then\n    _ = return 1\nend\n",
        }, "source": "root.tc",
        "include_roots": ["tc-tool-sem-0007/libs"],
    },
    {
        "id": "TC-TOOL-SEM-0008", "spec": "§4.6 public/private 可见性与 public const 初始化器引用 private const（值内联）",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\nvar x: int32 = copy int32, lib.B\n",
            "lib.tc": """#lib
private struct P then
    var x: int32
end
private const A: int32 = 1
public const B: int32 = Self.A
public static var C: int32 = Self.B
private func hidden(p: P) int32 then
    _ = return p.x
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0009", "spec": "§13.2.1 32 位档案：ptr/fnptr/isize/usize 为 4 字节，结构体布局按 W",
        "profile": "w32",
        "files": {"root.tc": """#program
struct S then
    var a: isize
    var b: ptr<int32>
    var c: fnptr<void>
    var d: usize
end
var n: usize = sizeof S
"""}, "source": "root.tc", "profile_argv": "w32",
    },
    {
        "id": "TC-TOOL-SEM-0010", "spec": "§13.2.1 64 位档案：ptr/fnptr/isize/usize 为 8 字节，结构体布局按 W",
        "profile": "w64",
        "files": {"root.tc": """#program
struct S then
    var a: isize
    var b: ptr<int32>
    var c: fnptr<void>
    var d: usize
end
var n: usize = sizeof S
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0011", "spec": "§13.2.2 read/write 的 isize/usize 按 W；32 位静态上下文接受",
        "profile": "w32",
        "files": {"root.tc": """#program
var x: isize = read isize
_ = write isize, x
var y: usize = read usize
_ = writeln usize, y
"""}, "source": "root.tc", "profile_argv": "w32",
    },
    {
        "id": "TC-TOOL-SEM-0012", "spec": "§13.2.2 read/write 的 isize/usize 按 W；64 位静态上下文接受",
        "profile": "w64",
        "files": {"root.tc": """#program
var x: isize = read isize
_ = write isize, x
var y: usize = read usize
_ = writeln usize, y
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0013", "spec": "§13.2.2 ptrdiff 结果为 isize，32 位宽度按 W",
        "profile": "w32",
        "files": {"root.tc": """#program
var a: int32 = copy int32, 0
var p: ptr<int32> = addr int32, a
var d: isize = ptrdiff int32, p, p
"""}, "source": "root.tc", "profile_argv": "w32",
    },
    {
        "id": "TC-TOOL-SEM-0014", "spec": "§13.2.2 ptrdiff 结果为 isize，64 位宽度按 W",
        "profile": "w64",
        "files": {"root.tc": """#program
var a: int32 = copy int32, 0
var p: ptr<int32> = addr int32, a
var d: isize = ptrdiff int32, p, p
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0015", "spec": "§13.2.2 bitcast ptr<T> <-> int 仅与 W 位整数等宽；32 位 ptr<->uint32",
        "profile": "w32",
        "files": {"root.tc": """#program
var p: ptr<int32> = copy ptr<int32>, nullptr
unsafe then
    var u: uint32 = bitcast uint32, p
    var q: ptr<int32> = bitcast ptr<int32>, u
end
"""}, "source": "root.tc", "profile_argv": "w32",
    },
    {
        "id": "TC-TOOL-SEM-0016", "spec": "§13.2.2 isize/usize 字面量范围按 W；64 位下 2^31/2^32 可表示",
        "profile": "w64",
        "files": {"root.tc": """#program
var a: isize = copy isize, 2147483648
var b: usize = copy usize, 4294967296
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0019", "spec": "§3.7 @padding(0) 与无 padding 均合法；无隐式对齐/尾填充",
        "profile": "common",
        "files": {"root.tc": """#program
struct S then
    var a: int8 @padding(0)
    var b: int32
end
var s: S = make S, 1, 2
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0020", "spec": "§3.8 fnptr<void> = 零参零返回；fnptr<void, T> 返回 void",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": """#lib
public func zero() void then
    _ = return
end
public func takes(a: int32) void then
    _ = return
end
public func f() int32 then
    var a: fnptr<void> = funcaddr Self.zero
    var b: fnptr<void, int32> = funcaddr Self.takes
    _ = return 0
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0021", "spec": "§6.6.3 cast 的 u 整数源类型 uint64；静态接受，范围由运行时检查",
        "profile": "common",
        "files": {"root.tc": """#program
var x: int8 = cast int8, 18446744073709551615u
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0022", "spec": "§3.11 bitcast fnptr<sig> -> fnptr<sig> 同签名合法",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": """#lib
public func f(a: int32) int32 then
    _ = return a
end
public func g() int32 then
    var p: fnptr<int32, int32> = funcaddr Self.f
    var q: fnptr<int32, int32> = bitcast fnptr<int32, int32>, p
    _ = return 0
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0023", "spec": "§3.7/§6.14 深度只读不延伸到 ptr 字段所指对象；unsafe 中经 ptr 字段写入",
        "profile": "common",
        "files": {"root.tc": """#program
struct S then
    readonly p: ptr<int32>
end
var s: S = make S, nullptr
unsafe then
    _ = store int32, s.p, 1
end
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0029", "spec": "§1.8/§4.11.2.6 布局按导入 DAG 逆向拓扑：钻石交叉边 b->c 时 b 的字段可用 c.S",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport a\nimport b\n",
            "a.tc": "#lib\nimport c\npublic const A: int32 = 1\n",
            "b.tc": "#lib\nimport c\npublic struct R then\n    var s: c.S\nend\n",
            "c.tc": "#lib\npublic struct S then\n    var x: int32\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0025", "spec": "§3.7/§4.11.2 导入模块的结构体在使用者字段中已定义完毕；布局按依赖优先",
        "profile": "common",
        "files": {
            "root.tc": """#program
import util
struct R then
    var s: util.S
end
var s: util.S = make util.S, 1
var r: R = make R, s
var x: int32 = copy int32, r.s.a
""" ,
            "util.tc": "#lib\npublic struct S then\n    var a: int32\nend\n",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0026", "spec": "§4.6 PRIVATE_TYPE 只适用于 public 签名/字段/const/static；public 函数体可访问本模块 private 类型",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": """#lib
private struct P then
    var a: int32
end
public func f() int32 then
    var p: P = make P, 1
    var n: usize = sizeof P
    var r: int32 = add int32, p.a, 1
    _ = return r
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0027", "spec": "§5.1/§5.6 局部值名可与模块成员/结构体名相同；局部优先，模块成员仍需 Self./m.",
        "profile": "common",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": """#lib
public struct S then
    var x: int32
end
public const K: int32 = 7
public func f() int32 then
    var K: int32 = copy int32, 1
    var S: int32 = copy int32, 2
    var x: int32 = add int32, K, S
    _ = return x
end
""",
        }, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0030", "spec": "§13.2.2 W=32 布局求和恰好为 usize 最大值时不溢出",
        "profile": "w32",
        "files": {"root.tc": """#program
struct S then
    var a: uint8 @padding(4294967294)
end
var n: usize = sizeof S
"""}, "source": "root.tc", "profile_argv": "w32",
    },
    {
        "id": "TC-TOOL-SEM-0031", "spec": "§13.2.2 W=64 布局求和恰好为 usize 最大值时不溢出",
        "profile": "w64",
        "files": {"root.tc": """#program
struct S then
    var a: uint8 @padding(18446744073709551614)
end
var n: usize = sizeof S
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0032", "spec": "§2.4 inf/nan 源类型由期望浮点类型决定；float32 上下文合法",
        "profile": "common",
        "files": {"root.tc": """#program
var a: float32 = copy float32, inf
var b: float32 = copy float32, -inf
var c: float32 = copy float32, nan
var d: float64 = copy float64, inf
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0033", "spec": "§6.10 br_if 条件字面量按 bool 检查；true 合法",
        "profile": "common",
        "files": {"root.tc": """#program
_ = br_if true, L
label L:
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0034", "spec": "§6.14 addr 可作用于 readonly 字段；readonly 只限制字段赋值",
        "profile": "common",
        "files": {"root.tc": """#program
struct S then
    readonly x: int32
end
var s: S = make S, 1
var p: ptr<int32> = addr int32, s.x
"""}, "source": "root.tc",
    },
    {
        "id": "TC-TOOL-SEM-0028", "spec": "§3.7/§13.2.2 W=64 布局求和 2^32 不溢出（W=32 会溢出）",
        "profile": "w64",
        "files": {"root.tc": """#program
struct S then
    var a: uint8 @padding(4294967295)
end
var n: usize = sizeof S
"""}, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0024", "spec": "§4.5 模块身份按解析路径：同名 shared.tc 在不同目录是两个实例，各自独立解析",
        "profile": "common",
        "files": {
            "root.tc": """#program
import a
import b
var x: int32 = call a.f
var y: int32 = call b.g
""",
            "left/a.tc": """#lib
import shared
public func f() int32 then
    _ = return shared.K
end
""",
            "right/b.tc": """#lib
import shared
public func g() int32 then
    _ = return shared.K
end
""",
            "left/shared.tc": "#lib\npublic const K: int32 = 1\n",
            "right/shared.tc": "#lib\npublic const K: int32 = 2\n",
        }, "source": "root.tc",
        "include_roots": [
            "tc-tool-sem-0024/left", "tc-tool-sem-0024/right",
        ],
    },
    {
        "id": "TC-TOOL-SEM-0017", "spec": "§8.6/§13.2.2 @align 的 N 在 64 位地址宽度内为合法 2 的幂",
        "profile": "w64",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f(@align(4294967296) p: ptr<int32>) int32 then\n    _ = return 0\nend\n",
        }, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
    {
        "id": "TC-TOOL-SEM-0018", "spec": "§8.6/§13.2.2 @range 的 isize 参数在 64 位下 2^32 可表示",
        "profile": "w64",
        "files": {
            "root.tc": "#program\nimport lib\n",
            "lib.tc": "#lib\npublic @fast func f(@range(0, 4294967296) n: isize) int32 then\n    _ = return 0\nend\n",
        }, "source": "root.tc", "profile_argv": "aarch64-linux-user",
    },
]


def _write_sem_cases(failures):
    sem_dir = HERE / "sem"
    sem_dir.mkdir(parents=True, exist_ok=True)
    for (case_id, source, profile, spec, text, stage, code,
         _related) in SEM_CASES:
        primary = _marker_position(text, MARK)
        related_marker = _marker_position(text, REL)
        if stage is not None and primary is None:
            failures.append(f"{case_id}: missing @HERE@")
            continue
        source_path = sem_dir / source
        yaml_path = sem_dir / (source[:-3] + ".yaml")
        stripped = _strip_markers(text)
        case = {"id": case_id, "source": source, "spec": spec,
                "stage": stage, "code": code, "profile": profile}
        if stage is None:
            profile_name = "w32" if profile == "w32" else "aarch64-linux-user"
            yaml = _tool_yaml(
                case,
                profile,
                ["tc-vm", "--static-only", f"--profile={profile_name}",
                 "@source"],
            )
            expected = [(source_path, stripped), (yaml_path, yaml)]
        else:
            related = None
            if related_marker is not None:
                related = (source, related_marker[0], related_marker[1])
            yaml = _compile_yaml(case, source, primary, related)
            expected = [(source_path, stripped), (yaml_path, yaml)]
        for path, content in expected:
            if path.exists() and path.read_text(encoding="utf-8") == content:
                continue
            if "--check" in sys.argv:
                failures.append(f"{case_id}: stale {path.relative_to(HERE)}")
            else:
                path.write_text(content, encoding="utf-8", newline="")
    return sem_dir


def _write_import_cases(failures, check):
    import_dir = HERE / "import"
    import_dir.mkdir(parents=True, exist_ok=True)
    for case in IMPORT_CASES:
        fixture = import_dir / "fixtures" / case["id"].lower()
        fixture.mkdir(parents=True, exist_ok=True)
        primary_file = None
        primary_pos = None
        related_file = None
        related_pos = None
        for name, text in case["files"].items():
            pos = _marker_position(text, MARK)
            if pos is not None:
                if primary_file is not None:
                    failures.append(f"{case['id']}: more than one @HERE@")
                primary_file = name
                primary_pos = pos
            rpos = _marker_position(text, REL)
            if rpos is not None:
                if related_file is not None:
                    failures.append(f"{case['id']}: more than one @REL@")
                related_file = name
                related_pos = rpos
            path = fixture / name
            path.parent.mkdir(parents=True, exist_ok=True)
            stripped = _strip_markers(text)
            if path.exists() and path.read_text(encoding="utf-8") == stripped:
                pass
            elif check:
                failures.append(
                    f"{case['id']}: stale {path.relative_to(HERE)}"
                )
            else:
                path.write_text(stripped, encoding="utf-8", newline="")
        if primary_file is None:
            failures.append(f"{case['id']}: missing @HERE@")
            continue
        source = f"fixtures/{case['id'].lower()}/{case['source']}"
        primary = f"fixtures/{case['id'].lower()}/{primary_file}"
        related = None
        if related_file is not None:
            related = (
                f"fixtures/{case['id'].lower()}/{related_file}",
                related_pos[0],
                related_pos[1],
            )
        meta = dict(case)
        meta["source"] = source
        yaml = _compile_yaml(meta, primary, primary_pos, related)
        yaml_path = import_dir / f"{case['id'].lower()}.yaml"
        if yaml_path.exists() and yaml_path.read_text(encoding="utf-8") == yaml:
            continue
        if check:
            failures.append(f"{case['id']}: stale {yaml_path.relative_to(HERE)}")
        else:
            yaml_path.write_text(yaml, encoding="utf-8", newline="")
    return import_dir


def _write_tool_cases(failures, check):
    tool_dir = HERE / "accept_s3"
    tool_dir.mkdir(parents=True, exist_ok=True)
    for case in TOOL_CASES:
        fixture = tool_dir / case["id"].lower()
        fixture.mkdir(parents=True, exist_ok=True)
        for name, text in case["files"].items():
            path = fixture / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.exists() and path.read_text(encoding="utf-8") == text:
                pass
            elif check:
                failures.append(f"{case['id']}: stale {path.relative_to(HERE)}")
            else:
                path.write_text(text, encoding="utf-8", newline="")
        source = f"{case['id'].lower()}/{case['source']}"
        meta = {
            "id": case["id"], "source": source, "spec": case["spec"],
            "include_roots": case.get("include_roots", []),
        }
        argv = ["tc-vm", "--static-only"]
        if "profile_argv" in case or case.get("profile") == "w32":
            name = case.get("profile_argv", "w32")
            argv.append(f"--profile={name}")
        argv.append("@source")
        yaml = _tool_yaml(meta, case["profile"], argv)
        yaml_path = tool_dir / f"{case['id'].lower()}.yaml"
        if yaml_path.exists() and yaml_path.read_text(encoding="utf-8") == yaml:
            continue
        if check:
            failures.append(f"{case['id']}: stale {yaml_path.relative_to(HERE)}")
        else:
            yaml_path.write_text(yaml, encoding="utf-8", newline="")
    return tool_dir


def _expected_artifacts():
    sem_paths = set()
    import_paths = set()
    tool_paths = set()
    for (case_id, source, _profile, _spec, _text, _stage, _code,
         _related) in SEM_CASES:
        sem_paths.add(HERE / "sem" / source)
        sem_paths.add(HERE / "sem" / (source[:-3] + ".yaml"))
    for case in IMPORT_CASES:
        fixture = HERE / "import" / "fixtures" / case["id"].lower()
        for name in case["files"]:
            import_paths.add(fixture / name)
        import_paths.add(HERE / "import" / f"{case['id'].lower()}.yaml")
    for case in TOOL_CASES:
        fixture = HERE / "accept_s3" / case["id"].lower()
        for name in case["files"]:
            tool_paths.add(fixture / name)
        tool_paths.add(HERE / "accept_s3" / f"{case['id'].lower()}.yaml")
    return sem_paths, import_paths, tool_paths


def _actual_artifacts(root):
    return {
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix in {".tc", ".yaml"}
    }


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    if not args.write and not args.check:
        parser.error("choose --write or --check")
    failures = []
    _write_sem_cases(failures)
    _write_import_cases(failures, args.check)
    _write_tool_cases(failures, args.check)
    expected_sem, expected_import, expected_tool = _expected_artifacts()
    for label, expected, root in (
        ("sem", expected_sem, HERE / "sem"),
        ("import", expected_import, HERE / "import"),
        ("accept_s3", expected_tool, HERE / "accept_s3"),
    ):
        for stale in sorted(_actual_artifacts(root) - expected):
            failures.append(
                f"{label}: stale generated file {stale.relative_to(HERE)}"
            )
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print(
        f"s2/s3 case table: {len(SEM_CASES)} sem/flip, "
        f"{len(IMPORT_CASES)} import/sem-fixture, "
        f"{len(TOOL_CASES)} tool cases"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

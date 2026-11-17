#!/usr/bin/env python3
"""LEX/SYN conformance case table for TC 0.0.46.

The expectations are derived from the standard; this file is the auditable
source table for the generated `*.tc` / `*.yaml` files.  The marker `@HERE@`
in a source records the position the standard requires for the primary
diagnostic.  The generator removes the marker and computes the 1-based line and
Unicode-scalar column; it never computes a position from compiler output.

Cases whose position rule is not explicitly enumerated in standard §12.5.1
are marked `@convention` in their spec text: the primary position is the first
token that cannot be consumed by the EBNF parse (the start of the smallest
malformed sub-construct).  This is the deterministic reading of the default
diagnostic span rule in §12.5.1 for token sequences the table does not list.

Run:
    python3 tests/conformance/static/s1_cases.py --write
    python3 tests/conformance/static/s1_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARKER = "@HERE@"


def make_case(case_id, source_name, spec, text, stage, code):
    return {
        "id": case_id,
        "source": source_name,
        "spec": spec,
        "text": text,
        "stage": stage,
        "code": code,
    }


CASES = [
    # ------------------------------------------------------------------ LEX
    make_case(
        "TC-LEX-SYN-0001", "slash_slash.tc",
        "§2.1 字符集；§2.2 唯一注释形态；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 /、// 定位到首个 /",
        "#program\nvar x: int32 = copy int32, 7 @HERE@// not a comment\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0002", "slash_single.tc",
        "§2.1 字符集；§2.2 唯一注释形态；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 / 定位到首个 /",
        "#program\nvar x: int32 = copy int32, 7 @HERE@/ 2\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0003", "semicolon.tc",
        "§2.1 字符集；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 ; 定位到首个 ;",
        "#program\nvar x: int32 = copy int32, 7 @HERE@;\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0004", "percent.tc",
        "§2.1 字符集（% 不属于允许集）；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "#program\nvar x: int32 = copy int32, 7 @HERE@% 2\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0005", "hash_plain.tc",
        "§2.1 字符集（# 仅作为模式声明首字符）；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@#\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0006", "hash_bad_word.tc",
        "§2.1 字符集；§2.3 模式词仅为 #program/#lib；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@#programx\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0007", "backslash_mid.tc",
        "§2.2 续行只允许逻辑行末；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@\\ 2\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0008", "backslash_space.tc",
        "§2.2 \\ 后不得有空白再换行；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@\\ \n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0009", "backslash_eof.tc",
        "§2.2 文件末尾的孤立续行；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@\\",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0010", "at_keyword.tc",
        "§2.3 @ 后必须紧跟 IDENT/unsafe/readonly；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, 7 @HERE@@var\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0011", "at_eof.tc",
        "§2.3 @ 后必须紧跟注解名；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\n@HERE@@",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0012", "block_comment.tc",
        "§2.2 只有 /// 行注释；/ 不开始块注释；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 / 定位到首个 /",
        "#program\nvar x: int32 = copy int32, 7 @HERE@/* nope */\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0013", "minus_nan.tc",
        "§2.4 -nan 语法拒绝；§2.7.1 - 紧邻数字或 inf 才属于字面量；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: float64 = copy float64, @HERE@-nan\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0014", "plus_standalone.tc",
        "§2.7.1 + 只允许出现在浮点指数中；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "#program\nvar x: int32 = copy int32, 1 @HERE@+ 2\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0015", "int_lex_range.tc",
        "§2.4 整数字面量词法值 0..2^64-1；§12.3 TC_CE_LITERAL_LEX_RANGE；§12.5.1 定位到完整字面量首 Token",
        "#program\nvar x: uint64 = copy uint64, @HERE@18446744073709551616\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
    make_case(
        "TC-LEX-SYN-0016", "int_lex_range_neg.tc",
        "§2.4 整数字面量词法值范围；§12.3 TC_CE_LITERAL_LEX_RANGE；§12.5.1 负号属于字面量时定位到 -",
        "#program\nvar x: int64 = copy int64, @HERE@-18446744073709551616\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
    make_case(
        "TC-LEX-SYN-0017", "float_lex_range_inf.tc",
        "§2.4 有限值舍入为无穷 -> TC_CE_LITERAL_LEX_RANGE；§12.3；§12.5.1 定位到完整字面量首 Token",
        "#program\nvar x: float64 = copy float64, @HERE@1e400\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
    make_case(
        "TC-LEX-SYN-0018", "float_lex_range_zero.tc",
        "§2.4 非零有限值舍入为零 -> TC_CE_LITERAL_LEX_RANGE；§12.3；§12.5.1 定位到完整字面量首 Token",
        "#program\nvar x: float64 = copy float64, @HERE@1e-400\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
    make_case(
        "TC-LEX-SYN-0019", "float32_lex_range.tc",
        "§2.4 f/F -> float32 且精确舍入；有限值舍入为无穷 -> TC_CE_LITERAL_LEX_RANGE；§12.3；§12.5.1 定位到完整字面量首 Token",
        "#program\nvar x: float32 = copy float32, @HERE@1e40f\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
    make_case(
        "TC-LEX-SYN-0020", "leading_zero.tc",
        "§2.4 十进制不允许前导零（007 非法）；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, @HERE@007\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0021", "underscore_zero.tc",
        "§2.4 0_0 非法（下划线只能在数字之间）；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: int32 = copy int32, @HERE@0_0\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0022", "float_dot_trailing.tc",
        "§2.4 浮点 . 两侧都要有数字（1. 非法）；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: float64 = copy float64, @HERE@1.\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0013", "float_dot_leading.tc",
        "§14.2 FLOAT 文法不含前导 .；§14.4 op_arg 不能以 . 开始；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（.）",
        "#program\nvar x: float64 = copy float64, @HERE@.5\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0024", "negative_unsigned.tc",
        "§2.4 - 只与无后缀字面量组合，-42u 非法；§14.2 INT 文法；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\nvar x: uint32 = copy uint32, @HERE@-42u\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0025", "non_ascii.tc",
        "§2.1 注释外只允许 ASCII；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "#program\nvar x: int32 = copy int32, 1 @HERE@é\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0026", "bom.tc",
        "§1.6/§2.1 不接受 BOM；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "@HERE@\ufeff#program\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0027", "nul_byte.tc",
        "§2.1 U+0000 非法；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "#program\nvar x: int32 = copy int32, 1 @HERE@\x00\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0028", "crlf_position.tc",
        "§2.5/§14.2 CRLF 为单个行终止符；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 ; 定位到 ;",
        "#program\r\nvar x: int32 = copy int32, 1\r\n_ = trap @HERE@;\r\n",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0029", "lone_cr_position.tc",
        "§2.5/§14.2 CR 为行终止符；§12.3 TC_CE_SYNTAX；§12.5.1 注释外 ; 定位到 ;",
        "#program\rvar x: int32 = copy int32, 1\r_ = trap @HERE@;\r",
        "LEX", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0030", "comment_unicode_nul.tc",
        "§2.1 注释内允许 Unicode 标量（U+0000 除外）；§12.3 TC_CE_SYNTAX；§12.5.1 非法标量定位到该标量",
        "#program\n/// unicode ✓ @HERE@\x00\n",
        "LEX", "TC_CE_SYNTAX"),

    # ------------------------------------------------------- SYN: block ends
    make_case(
        "TC-SYN-END-0001", "missing_end_struct.tc",
        "§12.3 TC_CE_MISSING_END（struct 缺 end）；§12.5.1 定位到未配对开启关键字",
        "#program\n@HERE@struct S then\n    readonly x: int32\n",
        "SYN", "TC_CE_MISSING_END"),
    make_case(
        "TC-SYN-END-0002", "missing_end_func.tc",
        "§12.3 TC_CE_MISSING_END（func 缺 end）；§12.5.1 定位到未配对开启关键字",
        "#lib\npublic @HERE@func f() int32 then\n    _ = return 1\n",
        "SYN", "TC_CE_MISSING_END"),
    make_case(
        "TC-SYN-END-0003", "missing_end_unsafe.tc",
        "§12.3 TC_CE_MISSING_END（unsafe 缺 end）；§12.5.1 定位到未配对开启关键字",
        "#program\n@HERE@unsafe then\n    _ = trap\n",
        "SYN", "TC_CE_MISSING_END"),
    make_case(
        "TC-SYN-END-0004", "missing_end_earliest.tc",
        "§12.3 TC_CE_MISSING_END；§12.5.1 多个候选时取源位置最早者",
        "#program\n@HERE@unsafe then\n    unsafe then\n        _ = trap\n",
        "SYN", "TC_CE_MISSING_END"),
    make_case(
        "TC-SYN-END-0005", "missing_end_before_member.tc",
        "§4.2 层序；§12.3 TC_CE_MISSING_END；§12.5.1 未配对开启关键字（最早的 struct）",
        "#lib\npublic @HERE@struct S then\n    readonly x: int32\npublic func f() void then\nend\n",
        "SYN", "TC_CE_MISSING_END"),

    # ----------------------------------------------------- SYN: module layer
    make_case(
        "TC-SYN-LAYER-0001", "layer_import_after_struct.tc",
        "§4.2 #lib 层序（import 在类型/值之前）；§12.3 TC_CE_MODULE_LAYER；§12.5.1 破坏层序的最早构造首 Token",
        "#lib\npublic struct S then\n    readonly x: int32\nend\n@HERE@import m\n",
        "SYN", "TC_CE_MODULE_LAYER"),
    make_case(
        "TC-SYN-LAYER-0002", "layer_struct_after_const.tc",
        "§4.2 #lib 层序（struct 在 const/static 之前）；§12.3 TC_CE_MODULE_LAYER；§12.5.1 构造首 Token（public）",
        "#lib\npublic const K: int32 = 1\n@HERE@public struct S then\n    readonly x: int32\nend\n",
        "SYN", "TC_CE_MODULE_LAYER"),
    make_case(
        "TC-SYN-LAYER-0003", "layer_const_after_func.tc",
        "§4.2 #lib 层序（const/static 在 func 之前）；§12.3 TC_CE_MODULE_LAYER；§12.5.1 构造首 Token（public）",
        "#lib\npublic func f() void then\nend\n@HERE@public const K: int32 = 1\n",
        "SYN", "TC_CE_MODULE_LAYER"),
    make_case(
        "TC-SYN-LAYER-0004", "layer_program_struct.tc",
        "§4.2 #program 层序（struct 在指令体之前）；§12.3 TC_CE_MODULE_LAYER；§12.5.1 破坏层序的最早构造首 Token",
        "#program\nvar x: int32 = copy int32, 1\n@HERE@struct S then\n    readonly y: int32\nend\n",
        "SYN", "TC_CE_MODULE_LAYER"),
    make_case(
        "TC-SYN-LAYER-0005", "layer_program_import.tc",
        "§4.2 #program 层序（import 连续且在 struct/指令之前）；§12.3 TC_CE_MODULE_LAYER；§12.5.1 破坏层序的最早构造首 Token",
        "#program\nstruct S then\n    readonly y: int32\nend\n@HERE@import m\n",
        "SYN", "TC_CE_MODULE_LAYER"),

    # ------------------------------------------------ SYN: missing visibility
    make_case(
        "TC-SYN-VIS-0001", "missing_visibility_struct.tc",
        "§4.2 #lib 成员必须 public/private；§12.3 TC_CE_MISSING_VISIBILITY；§12.5.1 缺少可见性的成员关键字",
        "#lib\n@HERE@struct S then\n    readonly x: int32\nend\n",
        "SYN", "TC_CE_MISSING_VISIBILITY"),
    make_case(
        "TC-SYN-VIS-0002", "missing_visibility_func.tc",
        "§4.2 #lib 成员必须 public/private；§12.3 TC_CE_MISSING_VISIBILITY；§12.5.1 缺少可见性的成员关键字",
        "#lib\n@HERE@func f() void then\nend\n",
        "SYN", "TC_CE_MISSING_VISIBILITY"),
    make_case(
        "TC-SYN-VIS-0003", "missing_visibility_static.tc",
        "§4.2 #lib 成员必须 public/private；§12.3 TC_CE_MISSING_VISIBILITY；§12.5.1 缺少可见性的成员关键字",
        "#lib\n@HERE@static var x: int32 = 1\n",
        "SYN", "TC_CE_MISSING_VISIBILITY"),
    make_case(
        "TC-SYN-VIS-0004", "missing_visibility_const.tc",
        "§4.2 #lib 成员必须 public/private；§12.3 TC_CE_MISSING_VISIBILITY；§12.5.1 缺少可见性的成员关键字",
        "#lib\n@HERE@const K: int32 = 1\n",
        "SYN", "TC_CE_MISSING_VISIBILITY"),
    make_case(
        "TC-SYN-VIS-0005", "missing_visibility_annotated_func.tc",
        "§4.2/§8.1 注解位于 func 之前但可见性仍是必需的；§12.3 TC_CE_MISSING_VISIBILITY；§12.5.1 成员关键字 func",
        "#lib\n@fast @HERE@func f() void then\nend\n",
        "SYN", "TC_CE_MISSING_VISIBILITY"),

    # ------------------------------------------------ SYN: program mode misuse
    make_case(
        "TC-SYN-MODE-0001", "program_func.tc",
        "§4.2 #program 不得使用 func；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 func 关键字",
        "#program\n@HERE@func f() void then\nend\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0002", "program_static.tc",
        "§4.2 #program 不得使用 static；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 static 关键字",
        "#program\n@HERE@static var x: int32 = 1\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0003", "program_public.tc",
        "§4.2 #program 不得使用 public/private；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 public 关键字",
        "#program\n@HERE@public struct S then\n    readonly x: int32\nend\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0004", "program_private.tc",
        "§4.2 #program 不得使用 public/private；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 private 关键字",
        "#program\n@HERE@private struct S then\n    readonly x: int32\nend\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0005", "program_self_assign.tc",
        "§4.2/§4.8 #program 不得使用 Self；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 Self 关键字",
        "#program\n@HERE@Self.x = copy int32, 1\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0006", "program_self_value.tc",
        "§4.2/§4.8 #program 不得使用 Self；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 Self 关键字",
        "#program\nvar x: int32 = copy int32, 1\nvar y: int32 = copy int32, @HERE@Self.x\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0007", "lib_var.tc",
        "§4.2 #lib 顶层不得使用 var/指令；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用的 var 关键字",
        "#lib\n@HERE@var x: int32 = copy int32, 1\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0008", "lib_instruction.tc",
        "§4.2 #lib 顶层不得使用指令；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用构造首 Token",
        "#lib\n@HERE@_ = trap\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-MODE-0009", "lib_label.tc",
        "§4.2 #lib 顶层不得使用指令/标签；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 误用构造首 Token",
        "#lib\n@HERE@label L:\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),

    # ------------------------------------------------- SYN: var missing init
    make_case(
        "TC-SYN-VAR-0001", "var_missing_eq.tc",
        "§5.2/§12.3 TC_CE_VAR_MISSING_INIT；§12.5.1 定位到 var 的标识符",
        "#program\nvar @HERE@x: int32\n",
        "SYN", "TC_CE_VAR_MISSING_INIT"),
    make_case(
        "TC-SYN-VAR-0002", "var_missing_rhs.tc",
        "§5.2/§12.3 TC_CE_VAR_MISSING_INIT；§12.5.1 定位到 var 的标识符",
        "#program\nvar @HERE@x: int32 =\n",
        "SYN", "TC_CE_VAR_MISSING_INIT"),
    make_case(
        "TC-SYN-VAR-0003", "static_var_missing_eq.tc",
        "§5.2/§12.3 TC_CE_VAR_MISSING_INIT（含 static var）；§12.5.1 定位到 var 的标识符",
        "#lib\npublic static var @HERE@x: int32\n",
        "SYN", "TC_CE_VAR_MISSING_INIT"),
    make_case(
        "TC-SYN-VAR-0004", "static_var_missing_rhs.tc",
        "§5.2/§12.3 TC_CE_VAR_MISSING_INIT（含 static var）；§12.5.1 定位到 var 的标识符",
        "#lib\npublic static var @HERE@x: int32 =\n",
        "SYN", "TC_CE_VAR_MISSING_INIT"),

    # --------------------------------------------- SYN: DISCARD positions etc.
    make_case(
        "TC-SYN-DISCARD-0001", "discard_decl_name.tc",
        "§2.3/§14.3 DISCARD 不得作标识符；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#program\nvar @HERE@_: int32 = copy int32, 1\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-DISCARD-0002", "discard_param_name.tc",
        "§2.3/§14.3 DISCARD 不得作形参名；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#lib\npublic func f(@HERE@_, x: int32) void then\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-DISCARD-0003", "discard_field_name.tc",
        "§2.3/§14.3 DISCARD 不得作字段名；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#lib\npublic struct S then\n    readonly @HERE@_: int32\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-DISCARD-0004", "discard_label_name.tc",
        "§2.3/§14.4 DISCARD 不得作标签名；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#program\nlabel @HERE@_:\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-DISCARD-0005", "discard_import_name.tc",
        "§2.3/§14.3 DISCARD 不得作导入名；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#program\nimport @HERE@_\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-DISCARD-0006", "discard_field_access.tc",
        "§2.3/§14.4 DISCARD 不得作字段名；§12.3 TC_CE_SYNTAX；§12.5.1 DISCARD 定位到 _",
        "#program\nvar s: int32 = copy int32, 1\nvar x: int32 = copy int32, s.@HERE@_\n",
        "SYN", "TC_CE_SYNTAX"),

    # ------------------------------------------- SYN: opcode/suffix and forms
    make_case(
        "TC-SYN-OP-0001", "unknown_opcode.tc",
        "§6.15.1 未知助记符属 SYN；§12.3 TC_CE_SYNTAX；§12.5.1 未知 opcode 定位到助记符首 Token",
        "#program\n_ = @HERE@frobnicate int32, 1\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0002", "unknown_suffix.tc",
        "§6.15.1 未知后缀属 SYN；§12.3 TC_CE_SYNTAX；§12.5.1 未知后缀定位到助记符首 Token",
        "#program\n_ = @HERE@add.foo int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0003", "undefined_combo.tc",
        "§6.15.1 未定义的 (base,suffix) 组合（div.wrap）属 SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\n_ = @HERE@div.wrap int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0004", "nested_op.tc",
        "§6.1 规则 6 禁止嵌套 op；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（嵌套 op 的助记符）",
        "#program\nvar x: int32 = copy int32, @HERE@add int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0005", "bare_value_init.tc",
        "§4.11.3/§6.12 var 初始化器必须是一个 op，不能是裸值；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#program\nvar x: int32 = @HERE@5\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0006", "bare_value_assign.tc",
        "§6.1/§6.12 赋值必须写 copy 或其它 op，不能是裸值；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#program\nx = @HERE@5\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0007", "missing_final_nl.tc",
        "§14.1/§14.3 NL 表示声明/指令结束；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（EOF）",
        "#program\nvar x: int32 = copy int32, 1@HERE@",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0008", "second_pattern.tc",
        "§4.1 恰好一个模式声明，模式后出现另一模式属 SYN；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "#program\n@HERE@#lib\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0009", "missing_pattern.tc",
        "§4.1 首条有效行必须是 #program/#lib；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "@HERE@var x: int32 = copy int32, 1\n",
        "SYN", "TC_CE_SYNTAX"),

    # ------------------------------------------------- SYN: grammar failures
    make_case(
        "TC-SYN-END-0006", "nested_paired_struct_func.tc",
        "§8.7 嵌套函数不受支持；§14.3/§14.4 struct 体只允许字段；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（public）",
        "#lib\n"
        "public struct S then\n"
        "    readonly x: int32\n"
        "    @HERE@public func f() void then\n"
        "    end\n"
        "end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-END-0007", "nested_paired_func.tc",
        "§8.7 嵌套函数不受支持；§14.3 func 体语句不含 func；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（public）",
        "#lib\n"
        "public func f() void then\n"
        "    @HERE@public func g() void then\n"
        "    end\n"
        "end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-END-0008", "nested_inner_func_one_end.tc",
        "§12.3 TC_CE_MISSING_END（只在实际缺 end 时）；§12.5.1 未配对开启关键字取源位置最早者；嵌套 func 的 then/end 配对",
        "#lib\n"
        "public @HERE@func f() void then\n"
        "    public func g() void then\n"
        "end\n",
        "SYN", "TC_CE_MISSING_END"),
    make_case(
        "TC-SYN-END-0009", "nested_paired_struct.tc",
        "§14.3 struct 体只允许字段（不支持嵌套 struct）；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（public）",
        "#lib\n"
        "public struct S then\n"
        "    readonly x: int32\n"
        "    @HERE@public struct T then\n"
        "        readonly y: int32\n"
        "    end\n"
        "end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-END-0010", "nested_unsafe_in_struct.tc",
        "§14.3 struct 体只允许字段；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（unsafe）",
        "#lib\n"
        "public struct S then\n"
        "    readonly x: int32\n"
        "    @HERE@unsafe then\n"
        "    end\n"
        "end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-SELF-0001", "program_struct_self.tc",
        "§4.2/§4.8 #program 不得使用 Self；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 Self 关键字",
        "#program\n"
        "struct @HERE@Self then\n"
        "    readonly x: int32\n"
        "end\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-SELF-0002", "program_var_self.tc",
        "§4.2/§4.8 #program 不得使用 Self；§12.3 TC_CE_PROGRAM_MODE_MISUSE；§12.5.1 Self 关键字",
        "#program\n"
        "var @HERE@Self: int32 = copy int32, 1\n",
        "SYN", "TC_CE_PROGRAM_MODE_MISUSE"),
    make_case(
        "TC-SYN-OP-0010", "incomplete_suffix.tc",
        "§12.5.1 未知后缀定位到助记符首 Token；§2.7.1 后缀属于助记符；不完整后缀 `add.` 作为无效后缀 Token",
        "#program\n"
        "_ = @HERE@add. int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0011", "separated_suffix.tc",
        "§2.7.1 后缀属于助记符的一部分（不拆成独立 Token）；空白分隔的 .wrap 是 Token 序列错误；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（.）",
        "#program\n"
        "_ = add @HERE@.wrap int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),

    make_case(
        "TC-SYN-GRAM-0001", "struct_missing_colon.tc",
        "§14.3 field_def 需要 \":\"；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#program\nstruct S then\n    readonly x @HERE@int32\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0002", "struct_missing_then.tc",
        "§14.3 struct_def 需要 then；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#program\nstruct S@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0003", "func_missing_then.tc",
        "§14.3 func_def 需要 then；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#lib\npublic func f() void @HERE@end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0004", "param_missing_type.tc",
        "§14.3 param 需要 \":\" type；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#lib\npublic func f(x: @HERE@) void then\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0005", "import_qualified.tc",
        "§14.3 import_decl = import IDENT NL；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#lib\nimport m@HERE@.n\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0006", "top_level_end.tc",
        "§14.3 #program 顶层语句不含游离 end；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#program\n@HERE@end\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0007", "label_missing_colon.tc",
        "§14.4 label_def 需要 \"label\" IDENT \":\"；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token",
        "#program\nlabel L@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0008", "assign_missing_op.tc",
        "§14.4 instruction = lhs \"=\" op_form NL；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#program\nvar x: int32 = copy int32, 1\nx =@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0009", "const_missing_eq.tc",
        "§14.3 const_decl 需要 \"=\" const_expr；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#lib\npublic const K: int32@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0010", "const_missing_init.tc",
        "§14.3 const_decl 需要 const_expr；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#lib\npublic const K: int32 =@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0011", "unsafe_missing_then.tc",
        "§14.4 unsafe_block 需要 then；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#program\nunsafe@HERE@\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-GRAM-0012", "field_padding_no_args.tc",
        "§14.3 field_padding = ANNOTATION \"(\" INT \")\"；§12.3 TC_CE_SYNTAX；§12.5.1 @convention 首个不可消费 Token（行终止符）",
        "#lib\npublic struct S then\n    readonly x: int32 @padding@HERE@\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0012", "suffix_load_wrap.tc",
        "§6.15.1 (load,wrap) 组合从未定义 -> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nvar p: ptr<int32> = copy ptr<int32>, nullptr\nunsafe then\nvar x: int32 = @HERE@load.wrap int32, p\nend\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0013", "suffix_br_wrap.tc",
        "§6.15.1 (br,wrap) 组合从未定义 -> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nlabel L:\n_ = @HERE@br.wrap L\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0014", "suffix_shr_wrap.tc",
        "§6.15.1 (shr,wrap) 组合从未定义（.wrap 只定义于 add/sub/mul/shl/neg）-> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nvar x: int32 = @HERE@shr.wrap int32, 1, 1\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0015", "suffix_and_nsw.tc",
        "§6.15.1/§10.9 .nsw/.nuw 只定义于 add/sub/mul/shl；(and,nsw) 组合 -> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nvar x: int32 = @HERE@and.nsw int32, 1, 2\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0016", "suffix_shr_nsw.tc",
        "§6.15.1/§10.9 .nsw/.nuw 只定义于 add/sub/mul/shl；(shr,nsw) 组合 -> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nvar x: int32 = @HERE@shr.nsw int32, 1, 1\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-SYN-OP-0017", "suffix_cast_nsw.tc",
        "§6.15.1/§10.9 .nsw/.nuw 只定义于 add/sub/mul/shl；(cast,nsw) 组合 -> SYN；§12.3 TC_CE_SYNTAX；§12.5.1 定位到助记符首 Token",
        "#program\nvar x: int8 = @HERE@cast.nsw int8, 1\n",
        "SYN", "TC_CE_SYNTAX"),
    make_case(
        "TC-LEX-SYN-0033", "stage_priority_lex_wins.tc",
        "§12.2 #1 阶段优先：全部 LEX 诊断先于全部 SYN 诊断；第 1 行 SYN 错误与第 2 行字面量词法超界并存时报 LITERAL_LEX_RANGE；§12.5.1 定位到完整字面量首 Token",
        "#program\n_ = frobnicate int32, 1\nvar x: int64 = copy int64, @HERE@99999999999999999999999999999999\n",
        "LEX", "TC_CE_LITERAL_LEX_RANGE"),
]

# Byte-exact L3 fixtures.  These cannot be expressed through the text/marker
# table: one pins the documented malformed-numeric-candidate recovery, the
# other exercises invalid UTF-8 line/column (including two non-ASCII scalars
# in a comment, where scalar and byte columns diverge).
RAW_CASES = [
    {
        "id": "TC-LEX-SYN-0031",
        "source": "multi_dot_float.tc",
        "group": "lex",
        "spec": "§2.4/§2.7.1/§14.2 数字产生式与最长匹配；畸形数字候选（多个小数点）按一个非法数字 Token 处理；§12.3 TC_CE_SYNTAX；§12.5.1 非法记号定位到记号首 Token",
        "data": b"#program\nvar x: float64 = copy float64, 1.5.5\n",
        "line": 2,
        "column": 32,
        "stage": "LEX",
        "code": "TC_CE_SYNTAX",
    },
    {
        "id": "TC-LEX-SYN-0032",
        "source": "invalid_utf8_l3.tc",
        "group": "lex",
        "spec": "§2.1/§12.5.1 非法 UTF-8 定位到首个非法字节；行列为最长有效前缀的标量计数；§12.3 TC_CE_SYNTAX",
        "data": b"#program\n/// \xc3\xa9\xc3\xa9\xff\n",
        "line": 2,
        "column": 7,
        "stage": "LEX",
        "code": "TC_CE_SYNTAX",
    },
]


def _raw_case_paths(case):
    group = case.get("group", "lex")
    return (
        HERE / group / f"{case['source'][:-3]}.yaml",
        HERE / group / case["source"],
    )


def _raw_case_yaml(case):
    source = case["source"]
    return (
        f"id: {case['id']}\n"
        f"profile: common\n"
        f"source: {source}\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        f"expect:\n"
        f"  stage: {case['stage']}\n"
        f"  code: {case['code']}\n"
        f"  primary: {{file: {source}, line: {case['line']}, column: {case['column']}}}\n"
        f"  related: []\n"
        f"  diagnostics: exactly-one\n"
    )


def _line_col(text):
    """Return the 1-based line and Unicode-scalar column of the marker."""
    index = text.index(MARKER)
    line = 1
    column = 1
    i = 0
    while i < index:
        ch = text[i]
        if ch == "\r":
            if i + 1 < len(text) and text[i + 1] == "\n":
                i += 2
            else:
                i += 1
            line += 1
            column = 1
        elif ch == "\n":
            i += 1
            line += 1
            column = 1
        else:
            i += 1
            column += 1
    return line, column


def _yaml_scalar(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r")
    return f'"{escaped}"'


def _case_paths(case):
    group = "lex" if case["stage"] == "LEX" else "syn"
    yaml_path = HERE / group / f"{case['source'][:-3]}.yaml"
    source_path = HERE / group / case["source"]
    return yaml_path, source_path


def _render_case(case):
    text = case["text"]
    if MARKER in text:
        line, column = _line_col(text)
        text = text.replace(MARKER, "")
    else:
        raise SystemExit(f"missing marker in {case['id']}")
    source_name = case["source"]
    yaml = (
        f"id: {case['id']}\n"
        f"profile: common\n"
        f"source: {source_name}\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        f"expect:\n"
        f"  stage: {case['stage']}\n"
        f"  code: {case['code']}\n"
        f"  primary: {{file: {source_name}, line: {line}, column: {column}}}\n"
        f"  related: []\n"
        f"  diagnostics: exactly-one\n"
    )
    return text, yaml


def write_cases():
    for case in CASES:
        text, yaml = _render_case(case)
        yaml_path, source_path = _case_paths(case)
        yaml_path.parent.mkdir(parents=True, exist_ok=True)
        source_path.write_text(text, encoding="utf-8", newline="")
        yaml_path.write_text(yaml, encoding="utf-8", newline="")
    for case in RAW_CASES:
        yaml_path, source_path = _raw_case_paths(case)
        yaml_path.parent.mkdir(parents=True, exist_ok=True)
        source_path.write_bytes(case["data"])
        yaml_path.write_text(_raw_case_yaml(case), encoding="utf-8", newline="")


def _check_unique_ids():
    failures = 0
    seen = {}
    for case in [*CASES, *RAW_CASES, *ACCEPT_CASES, *PROPERTY_CASES]:
        case_id = case["id"]
        if case_id in seen:
            print(f"DUPLICATE case id {case_id}")
            failures += 1
        seen[case_id] = True
    return failures


def _check_orphans(expected_paths):
    failures = 0
    expected = {path.resolve() for path in expected_paths}
    roots = [HERE / "lex", HERE / "syn", HERE / "accept", HERE.parent / "fuzz"]
    for root in roots:
        if not root.is_dir():
            continue
        for path in sorted(root.iterdir()):
            if path.suffix not in {".yaml", ".tc"}:
                continue
            if path.resolve() not in expected:
                print(f"ORPHAN {path}")
                failures += 1
    return failures


def check_cases():
    failures = _check_unique_ids()
    expected_paths = []
    for case in CASES:
        yaml_path, source_path = _case_paths(case)
        expected_paths.extend([yaml_path, source_path])
    for case in RAW_CASES:
        yaml_path, source_path = _raw_case_paths(case)
        expected_paths.extend([yaml_path, source_path])
    for case in ACCEPT_CASES:
        accept_dir = HERE / "accept"
        expected_paths.extend([
            accept_dir / case["source"],
            accept_dir / f"{case['source'][:-3]}.yaml",
        ])
    for case in PROPERTY_CASES:
        expected_paths.append(HERE.parent / "fuzz" / f"{case['id'].lower()}.yaml")
    failures += _check_orphans(expected_paths)
    for case in CASES:
        text, yaml = _render_case(case)
        yaml_path, source_path = _case_paths(case)
        for path, expected in ((yaml_path, yaml), (source_path, text)):
            if not path.exists():
                print(f"MISSING {path}")
                failures += 1
                continue
            actual = path.read_bytes()
            wanted = expected.encode("utf-8")
            if actual != wanted:
                print(f"STALE {path}")
                failures += 1
    for case in RAW_CASES:
        yaml_path, source_path = _raw_case_paths(case)
        for path, expected in ((yaml_path, _raw_case_yaml(case)), (source_path, case["data"])):
            if not path.exists():
                print(f"MISSING {path}")
                failures += 1
                continue
            actual = path.read_bytes()
            wanted = expected.encode("utf-8") if isinstance(expected, str) else expected
            if actual != wanted:
                print(f"STALE {path}")
                failures += 1
    if failures:
        print(f"case table: {failures} stale/missing file(s)")
        return 1
    print(f"case table: {len(CASES) + len(RAW_CASES)} cases match "
          "generated files")
    return 0


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write:
        write_cases()
        write_positive_cases()
        return 0
    failures = check_cases()
    failures += check_positive_cases()
    return 0 if failures == 0 else 1



# ---------------------------------------------------------------------------
# Positive LEX/SYN acceptance cases.  These are tool-kind cases: the adapter
# runs `tc-vm --syntax-only` and requires exit 0 with empty stdout/stderr.
# LEX/SYN acceptance is not a semantic validity claim.
# ---------------------------------------------------------------------------

ACCEPT_CASES = [
    {
        "id": "TC-TOOL-SYN-0001",
        "source": "lib_all_forms.tc",
        "spec": "§14 EBNF 全部产生式（#lib/import/struct/const/static var/func/注解/unsafe/标签/指令/类型/字面量）；LEX/SYN 接受",
        "text": """/// broad #lib coverage
#lib
import util

public struct Pair then
    readonly x: int32
    var y: float64 @padding(4)
end

private const K: int32 = 42
public static var counter: usize = 0

public @safe @inline func classify(n: int32, p: ptr<Pair>) int32 then
    var c: int32 = copy int32, 0
    label L0:
    _ = br_if true, L1
    _ = br L2
    label L1:
    c = add int32, n, Self.K
    _ = br L3
    label L2:
    c = sub int32, n, 1
    label L3:
    var ap: ptr<int32> = addr int32, c
    unsafe then
        _ = store int32, ap, n
    end
    _ = return c
end

private @fast @no_overflow func raw(@range(0, 100) n: int32, @nonnull @align(8) p: ptr<int64>) int64 then
    var v: int64 = load int64, p
    _ = return v
end
""",
    },
    {
        "id": "TC-TOOL-SYN-0002",
        "source": "program_all_forms.tc",
        "spec": "§14 EBNF #program 全部产生式（import/struct/顶层 const/var/label/unsafe/字段 lvalue/命名实参）；LEX/SYN 接受",
        "text": """#program
import util
struct S then
    readonly flag: bool
    var count: int32
end
const K: int32 = 7
var s: S = make S, flag: true, count: 1
var n: usize = sizeof S
var a: int32 = add int32, s.count, K
var ap: ptr<int32> = addr int32, n
_ = br_if s.flag, L0
_ = br L1
label L0:
s.count = copy int32, 2
label L1:
unsafe then
    _ = store int32, ap, a
end
""",
    },
    {
        "id": "TC-TOOL-SYN-0003",
        "source": "identifiers.tc",
        "spec": "§2.3/§14.2 IDENT 与 DISCARD：_x/x_/_1/__ 是合法标识符，小写 self 不是关键字；LEX/SYN 接受",
        "text": """#program
var _x: int32 = copy int32, 1
var x_: int32 = copy int32, 2
var _1: int32 = copy int32, 3
var __: int32 = copy int32, 4
var self: int32 = copy int32, 5
""",
    },
    {
        "id": "TC-TOOL-SYN-0004",
        "source": "literal_forms.tc",
        "spec": "§2.4/§14.2 整数字面量（进制/下划线/u/负值）、浮点（小数/指数/f）、inf/-inf/nan、bool/nullptr；LEX/SYN 接受",
        "text": """#program
var a: int32 = copy int32, -42
var b: uint32 = copy uint32, 42u
var c: int32 = copy int32, 0x2A
var d: int32 = copy int32, 0b101010
var e: int32 = copy int32, 0o52
var f: int32 = copy int32, 1_000
var g: float64 = copy float64, 1.5
var h: float64 = copy float64, 1e10
var i: float32 = copy float32, 0.5f
var j: float64 = copy float64, -0.0
var k: float64 = copy float64, inf
var l: float64 = copy float64, -inf
var m: float64 = copy float64, nan
var n: bool = copy bool, true
var o: bool = copy bool, false
var p: ptr<int32> = copy ptr<int32>, nullptr
""",
    },
    {
        "id": "TC-TOOL-SYN-0005",
        "source": "comments_continuation.tc",
        "spec": "§2.2 注释与续行；§2.1 注释内 Unicode；LEX/SYN 接受",
        "text": """#program
/// Unicode comment ✓ 中文
var x: int32 = copy int32, \\
    1
/// trailing comment
var y: int32 = add int32, x, \\
    2
""",
    },
    {
        "id": "TC-TOOL-SYN-0006",
        "source": "crlf.tc",
        "spec": "§2.5/§14.2 CRLF 为单个行终止符；LEX/SYN 接受",
        "text": "#program\r\nvar x: int32 = copy int32, 1\r\n_ = trap\r\n",
    },
    {
        "id": "TC-TOOL-SYN-0007",
        "source": "lone_cr.tc",
        "spec": "§2.5/§14.2 CR 为行终止符；LEX/SYN 接受",
        "text": "#program\rvar x: int32 = copy int32, 1\r_ = trap\r",
    },
    {
        "id": "TC-TOOL-SYN-0008",
        "source": "annotations.tc",
        "spec": "§2.6/§8.6/§14.3 注解产生式与全部 16 个注解名（@unsafe/@readonly 为关键字名，其余为 IDENT）；LEX/SYN 接受",
        "text": """#lib
public struct P then
    readonly x: int32 @padding(4)
end
public @inline func f0() void then
end
public @noinline func f1() void then
end
public @unsafe func f2() void then
end
public @safe func f3() void then
end
public @fast func f4(@nonnull p: ptr<int32>, @no_alias q: ptr<int32>, @align(8) r: ptr<int32>, @range(0, 10) n: int32) void then
end
public @fast @fast_math @pure @readonly @nounwind @willreturn @no_overflow func f5() void then
end
""",
    },
    {
        "id": "TC-TOOL-SYN-0009",
        "source": "types.tc",
        "spec": "§14.5 全部类型产生式（标量/void/ptr<T>/fnptr<ret, params...>/结构体名/限定结构体名）；LEX/SYN 接受",
        "text": """#lib
import util
public struct S then
    readonly x: int32
end
public func f(a: bool, b: int8, c: int16, d: int32, e: int64, f8: uint8, f16: uint16, f32: uint32, f64: uint64, j: isize, k: usize, l: float32, m: float64, n: ptr<int32>, o: fnptr<int32, ptr<S>, bool>, p: S, q: ptr<ptr<S>>, r: fnptr<void>, s: util.T) void then
end
""",
    },
    {
        "id": "TC-TOOL-SYN-0010",
        "source": "switch_named.tc",
        "spec": "§6.1/§6.2.8/§14.4 switch 多目标、命名实参、命名字段、字段 lvalue/value；LEX/SYN 接受",
        "text": """#lib
public struct S then
    readonly a: int32
    var b: int32
end
public func h(first: int32, second: S) int32 then
    _ = return first
end
public func g(s: S) int32 then
    var t: S = make S, a: 1, b: 2
    var x: int32 = call Self.h, first: 1, second: s
    _ = switch x, L0, L1
    label L0:
    _ = br L2
    label L1:
    _ = br L2
    label L2:
    var y: int32 = copy int32, t.a
    _ = return y
end
""",
    },
]

PROPERTY_CASES = [
    {
        "id": "TC-FZ-LEX-0001",
        "spec": "§2/§12.1 fail-fast：随机字节不得崩溃/挂起，拒绝时恰好一个 LEX/SYN 诊断；§2/§12.1 模糊/属性",
        "trials": 200,
        "seed": 1,
    },
]


def _accept_yaml(case):
    return (
        f"id: {case['id']}\n"
        "profile: common\n"
        f"source: {case['source']}\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        "kind: tool\n"
        "tool:\n"
        "  argv: [tc-vm, --syntax-only, \"@source\"]\n"
        "  repeat: 2\n"
        "expect:\n"
        "  exit: 0\n"
        "  stdout_hex: \"\"\n"
        "  stderr_hex: \"\"\n"
    )


def _property_yaml(case):
    return (
        f"id: {case['id']}\n"
        "profile: common\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        "kind: property\n"
        f"property: {{name: lex_random, trials: {case['trials']}, seed: {case['seed']}}}\n"
        "expect: {}\n"
    )


def write_positive_cases():
    accept_dir = HERE / "accept"
    accept_dir.mkdir(parents=True, exist_ok=True)
    for case in ACCEPT_CASES:
        source_path = accept_dir / case["source"]
        source_path.write_text(case["text"], encoding="utf-8", newline="")
        yaml_path = accept_dir / f"{case['source'][:-3]}.yaml"
        yaml_path.write_text(_accept_yaml(case), encoding="utf-8", newline="")
    fuzz_dir = HERE.parent / "fuzz"
    fuzz_dir.mkdir(parents=True, exist_ok=True)
    for case in PROPERTY_CASES:
        yaml_path = fuzz_dir / f"{case['id'].lower()}.yaml"
        yaml_path.write_text(_property_yaml(case), encoding="utf-8", newline="")


def check_positive_cases():
    failures = 0
    accept_dir = HERE / "accept"
    for case in ACCEPT_CASES:
        expected = [
            (accept_dir / case["source"], case["text"]),
            (accept_dir / f"{case['source'][:-3]}.yaml", _accept_yaml(case)),
        ]
        for path, text in expected:
            if not path.exists() or path.read_bytes() != text.encode("utf-8"):
                print(f"STALE {path}")
                failures += 1
    for case in PROPERTY_CASES:
        path = HERE.parent / "fuzz" / f"{case['id'].lower()}.yaml"
        text = _property_yaml(case)
        if not path.exists() or path.read_bytes() != text.encode("utf-8"):
            print(f"STALE {path}")
            failures += 1
    if failures:
        print(f"positive case table: {failures} stale/missing file(s)")
        return 1
    print(
        f"positive case table: {len(ACCEPT_CASES)} accept + "
        f"{len(PROPERTY_CASES)} property cases match"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

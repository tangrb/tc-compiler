#!/usr/bin/env python3
"""S4 conformance case table for TC 0.0.46.

Covers CFG/reachability, definite initialization, missing returns, labels and
control instructions, call check order/safety, the unsafe-context matrix and
the annotation matrix.  Expectations are derived from the standard and design,
never from compiler output.  `@HERE@` marks the primary diagnostic position;
`@REL@` marks a related position.  The generator removes the markers and
computes 1-based line and Unicode-scalar columns from the source text.

Run:
    python3 tests/conformance/static/s4_cases.py --write
    python3 tests/conformance/static/s4_cases.py --check
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


def _tool_yaml(case, source, argv):
    args = ", ".join(f'"{item}"' for item in argv)
    return (
        f"id: {case['id']}\n"
        f"profile: {case.get('profile', 'common')}\n"
        f"source: {source}\n"
        f"spec: {_yaml_scalar(case['spec'])}\n"
        "kind: tool\n"
        "tool:\n"
        f"  argv: [{args}]\n"
        "  repeat: 2\n"
        "expect:\n"
        "  exit: 0\n"
        '  stdout_hex: ""\n'
        '  stderr_hex: ""\n'
    )


def _case(case_id, spec, files, stage, code, profile="common", source="root.tc"):
    return {
        "id": case_id,
        "spec": spec,
        "files": files,
        "stage": stage,
        "code": code,
        "profile": profile,
        "source": source,
    }


def _positive(case_id, spec, files, profile="common", source="root.tc"):
    return {
        "id": case_id,
        "spec": spec,
        "files": files,
        "profile": profile,
        "source": source,
    }


PROGRAM = "#program\n"
LIB = "#lib\n"

CASES = [
    # ------------------------------------------------------------------
    # Definite initialization (§5.4/§5.7.1).
    _case(
        "TC-SEM-DI-0001",
        "§5.4/§7.2 br 跳过 var 声明后读取：该路径未初始化 -> UNINITIALIZED；"
        "§12.5.1 读取标识符",
        {"root.tc": PROGRAM +
         "_ = br L\n"
         "var x: int32 = copy int32, 1\n"
         "label L:\n"
         "_ = write int32, @HERE@x\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0002",
        "§5.4 分支汇合取合取：一条路径经过 var，另一条绕过 -> UNINITIALIZED",
        {"root.tc": PROGRAM +
         "var c: bool = copy bool, true\n"
         "_ = br_if c, A\n"
         "_ = br M\n"
         "label A:\n"
         "var x: int32 = copy int32, 1\n"
         "label M:\n"
         "_ = write int32, @HERE@x\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0003",
        "§5.4 循环回边按前驱处理：循环体内 var 可被零次执行，循环外读取 -> "
        "UNINITIALIZED",
        {"root.tc": PROGRAM +
         "label L:\n"
         "_ = br_if true, B\n"
         "_ = br E\n"
         "label B:\n"
         "var x: int32 = copy int32, 1\n"
         "_ = br L\n"
         "label E:\n"
         "_ = write int32, @HERE@x\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0004",
        "§5.4/§5.7.1 已有变量作为赋值目标也要求已确定初始化 -> UNINITIALIZED；"
        "§12.5.1 目标标识符",
        {"root.tc": PROGRAM +
         "_ = br L\n"
         "var x: int32 = copy int32, 1\n"
         "label L:\n"
         "@HERE@x = copy int32, 2\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0005",
        "§5.4 已有变量的 read 赋值先要求变量已初始化 -> UNINITIALIZED",
        {"root.tc": PROGRAM +
         "_ = br L\n"
         "var x: int32 = copy int32, 1\n"
         "label L:\n"
         "@HERE@x = read int32\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0006",
        "§5.7.1/§7.5 switch 的全部目标都参与可达性与 DI；跳过 var 的目标 -> "
        "UNINITIALIZED",
        {"root.tc": PROGRAM +
         "var c: int32 = copy int32, 0\n"
         "_ = switch c, A, B\n"
         "label A:\n"
         "var x: int32 = copy int32, 1\n"
         "_ = br E\n"
         "label B:\n"
         "_ = br E\n"
         "label E:\n"
         "_ = write int32, @HERE@x\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0007",
        "§5.4 字段读取要求外层对象已初始化 -> UNINITIALIZED；位置为基址标识符",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var f: int32\n"
         "end\n"
         "_ = br L\n"
         "var s: S = make S, 1\n"
         "label L:\n"
         "_ = write int32, @HERE@s.f\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0008",
        "§5.4 字段赋值不改变外层对象的外层初始化状态：未初始化基址作为字段目标 "
        "-> UNINITIALIZED",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var f: int32\n"
         "end\n"
         "_ = br L\n"
         "var s: S = make S, 1\n"
         "label L:\n"
         "@HERE@s.f = copy int32, 2\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-DI-0009",
        "§5.7.1 每个函数是独立 DI 分析单元；函数内 br 跳过 var 后读取 -> "
        "UNINITIALIZED（导入模块内）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    _ = br L\n"
         "    var x: int32 = copy int32, 1\n"
         "    label L:\n"
         "    var y: int32 = copy int32, @HERE@x\n"
         "    _ = return y\n"
         "end\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    # ------------------------------------------------------------------
    # Missing returns and control flow (§7.7/§8.3).
    _case(
        "TC-SEM-CFG-0001",
        "§7.7.3 非 void 空函数体直接 MISSING_RETURN；§12.5.1 返回类型，关联 end",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0002",
        "§7.7.1/§7.7.3 br_if 常量条件仍保留 false 边；false 边落到 end -> "
        "MISSING_RETURN",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "    _ = br_if true, L\n"
         "    label L:\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0003",
        "§7.7.3 多个函数缺返回按模块内源序取最早者；§12.5.1 返回类型",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "@REL@end\n"
         "public func g() int32 then\n"
         "end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0004",
        "§7.7.3 多个导入模块都缺返回时按 module_order 取最早模块",
        {"root.tc": PROGRAM + "import a\nimport b\n",
         "a.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "@REL@end\n",
         "b.tc": LIB +
         "public func g() int32 then\n"
         "end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0019",
        "§7.7.1/§8.8.3 call 是调用者 CFG 的原子节点；被调函数缺返回在被调模块"
        "报告，不展开到调用者",
        {"root.tc": PROGRAM + "import lib\n"
         "var x: int32 = call lib.f\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0021",
        "§6.6.3/§12.2 #9 switch 的整数字面量先按其源类型（无后缀 int64）定型；"
        "2^63 不可表示 -> LITERAL_RANGE",
        {"root.tc": PROGRAM +
         "_ = switch @HERE@9223372036854775808, L\n"
         "label L:\n"},
        "SEM", "TC_CE_LITERAL_RANGE",
    ),
    _case(
        "TC-SEM-CFG-0020",
        "§7.1/§7.7.1 标签命名空间按函数/程序封闭；跨函数 br 目标 -> "
        "LABEL_NOT_FOUND",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    label L:\n"
         "end\n"
         "public func g() void then\n"
         "    _ = br @HERE@L\n"
         "end\n"},
        "SEM", "TC_CE_LABEL_NOT_FOUND",
    ),
    _case(
        "TC-SEM-CFG-0005",
        "§7.2/§12.3 标签命名空间重名 -> DUPLICATE_LABEL；§12.5.1 第二次标签名并关联首次",
        {"root.tc": PROGRAM +
         "label @REL@L:\n"
         "label @HERE@L:\n"
         "_ = br L\n"},
        "SEM", "TC_CE_DUPLICATE_LABEL",
    ),
    _case(
        "TC-SEM-CFG-0006",
        "§7.2/§12.3 br 目标不存在 -> LABEL_NOT_FOUND；§12.5.1 标签名",
        {"root.tc": PROGRAM + "_ = br @HERE@M\n"},
        "SEM", "TC_CE_LABEL_NOT_FOUND",
    ),
    _case(
        "TC-SEM-CFG-0007",
        "§7.5 switch 的任一目标不存在 -> LABEL_NOT_FOUND；§12.5.1 标签名",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 0\n"
         "_ = switch x, L, @HERE@M\n"
         "label L:\n"},
        "SEM", "TC_CE_LABEL_NOT_FOUND",
    ),
    _case(
        "TC-SEM-CFG-0008",
        "§7.3 br_if 条件不是 bool -> CONDITION_TYPE；§12.5.1 条件操作数",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 1\n"
         "_ = br_if @HERE@x, L\n"
         "label L:\n"},
        "SEM", "TC_CE_CONDITION_TYPE",
    ),
    _case(
        "TC-SEM-CFG-0009",
        "§7.5 switch 值不是整数 -> SWITCH_TYPE；§12.5.1 值操作数",
        {"root.tc": PROGRAM +
         "var b: bool = copy bool, true\n"
         "_ = switch @HERE@b, L\n"
         "label L:\n"},
        "SEM", "TC_CE_SWITCH_TYPE",
    ),
    _case(
        "TC-SEM-CFG-0010",
        "§6.2.8 switch 至少一个默认标签；零标签 -> OPERAND_COUNT（助记符）",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 0\n"
         "_ = @HERE@switch x\n"},
        "SEM", "TC_CE_OPERAND_COUNT",
    ),
    _case(
        "TC-SEM-CFG-0011",
        "§8.3 return 只在函数体内；#program 顶层 -> RETURN_OUTSIDE_FUNCTION；"
        "§12.5.1 return 关键字",
        {"root.tc": PROGRAM + "_ = @HERE@return 1\n"},
        "SEM", "TC_CE_RETURN_OUTSIDE_FUNCTION",
    ),
    _case(
        "TC-SEM-CFG-0012",
        "§8.3 return 操作数类型必须严格等于返回类型 -> RETURN_TYPE；"
        "§12.5.1 返回操作数",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    var b: bool = copy bool, true\n"
         "    _ = return @HERE@b\n"
         "end\n"},
        "SEM", "TC_CE_RETURN_TYPE",
    ),
    _case(
        "TC-SEM-CFG-0013",
        "§8.3 非 void 函数必须写 `_ = return x`，缺操作数 -> RETURN_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    _ = @HERE@return\n"
         "end\n"},
        "SEM", "TC_CE_RETURN_FORM",
    ),
    _case(
        "TC-SEM-CFG-0014",
        "§8.3 void 函数只能 `_ = return`，带操作数 -> RETURN_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    _ = @HERE@return 1\n"
         "end\n"},
        "SEM", "TC_CE_RETURN_FORM",
    ),
    _case(
        "TC-SEM-CFG-0015",
        "§7.7.2 不可达代码仍做名称/类型检查（只是不参与 DI/末尾传播）",
        {"root.tc": PROGRAM +
         "_ = br E\n"
         "var x: int32 = @HERE@copy int64, 1\n"
         "label E:\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    _case(
        "TC-SEM-CFG-0018",
        "§6.15.1 br_if 操作数数量不符 -> OPERAND_COUNT；畸形分支按保守 "
        "fallthrough 参与可达性（此处无更早 MISSING_RETURN 竞争）",
        {"root.tc": PROGRAM + "_ = @HERE@br_if true\n"},
        "SEM", "TC_CE_OPERAND_COUNT",
    ),
    _case(
        "TC-SEM-CFG-0016",
        "§12.2 #9 操作数内部优先：br_if 的浮点字面量先按 bool 期望类型报 "
        "LITERAL_TYPE，不报 CONDITION_TYPE/LABEL_NOT_FOUND",
        {"root.tc": PROGRAM +
         "_ = br_if @HERE@1.0, Missing\n"},
        "SEM", "TC_CE_LITERAL_TYPE",
    ),
    _case(
        "TC-SEM-CFG-0017",
        "§12.2 #9 操作数内部优先：return 的浮点字面量先按返回类型报 "
        "LITERAL_TYPE，不报 RETURN_TYPE",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    _ = return @HERE@1.0\n"
         "end\n"},
        "SEM", "TC_CE_LITERAL_TYPE",
    ),
    # ------------------------------------------------------------------
    # Call check order and call safety (§8.8.2/§12.2 #10).
    _case(
        "TC-SEM-CALL-0101",
        "§8.8.2/#10 命名实参重复先于其它实参错误 -> DUPLICATE_ARGUMENT；"
        "§12.5.1 第二重复名并关联首次",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = call lib.two, @REL@a: 1, @HERE@a: 2\n",
         "lib.tc": LIB +
         "public func two(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_DUPLICATE_ARGUMENT",
    ),
    _case(
        "TC-SEM-CALL-0102",
        "§8.8.2 命名实参未知 -> UNKNOWN_ARGUMENT；§12.5.1 最早未知实参名",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = call lib.two, a: 1, @HERE@z: 2\n",
         "lib.tc": LIB +
         "public func two(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_UNKNOWN_ARGUMENT",
    ),
    _case(
        "TC-SEM-CALL-0103",
        "§8.8.2 命名实参缺失先于数量 -> MISSING_ARGUMENT（助记符）；"
        "§12.5.1 关联声明顺序最靠前的缺失形参",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.two, a: 1\n",
         "lib.tc": LIB +
         "public func two(a: int32, @REL@b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_MISSING_ARGUMENT",
    ),
    _case(
        "TC-SEM-CALL-0104",
        "§8.8.2 位置实参数量不符 -> ARGUMENT_COUNT（助记符）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.one\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_ARGUMENT_COUNT",
    ),
    _case(
        "TC-SEM-CALL-0105",
        "§8.8.2 命名实参必须保持声明顺序 -> ARGUMENT_ORDER；"
        "§12.5.1 首个顺序不符的实参名",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = call lib.two, @HERE@b: 2, a: 1\n",
         "lib.tc": LIB +
         "public func two(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_ARGUMENT_ORDER",
    ),
    _case(
        "TC-SEM-CALL-0106",
        "§8.8.2 位置与命名实参不得混用 -> ARGUMENT_ORDER（首个命名实参）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = call lib.two, 1, @HERE@b: 2\n",
         "lib.tc": LIB +
         "public func two(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_ARGUMENT_ORDER",
    ),
    _case(
        "TC-SEM-CALL-0107",
        "§8.8.2 调用安全先于实参检查：@fast 目标与重复实参同时存在 -> FAST_CALL",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.ff, a: 1, a: 2\n",
         "lib.tc": LIB +
         "public @fast func ff(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0108",
        "§8.8.2/§9.2 安全代码调用 @unsafe 函数 -> UNSAFE_CALL（助记符）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.uu, 1\n",
         "lib.tc": LIB +
         "public @unsafe func uu(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_UNSAFE_CALL",
    ),
    _case(
        "TC-SEM-CALL-0109",
        "§8.8.2/§9.6 安全代码调用 @fast 函数 -> FAST_CALL",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.ff, 1\n",
         "lib.tc": LIB +
         "public @fast func ff(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0110",
        "§8.8.2/§9.6 @fast_math 函数也要求 unsafe 调用者 -> FAST_CALL",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.fm, 1\n",
         "lib.tc": LIB +
         "public @fast @fast_math func fm(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0111",
        "§9.3/§9.8 @safe 函数调用 @unsafe 函数 -> SAFE_VIOLATION 优先于 "
        "UNSAFE_CALL",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @unsafe func uu() void then\n"
         "end\n"
         "public @safe func f() void then\n"
         "    _ = @HERE@call Self.uu\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-CALL-0112",
        "§8.8.2 void 调用只能用 `_`；作为 var 初始化器 -> VOID（助记符）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.voidf\n",
         "lib.tc": LIB +
         "public func voidf() void then\n"
         "    _ = return\n"
         "end\n"},
        "SEM", "TC_CE_VOID",
    ),
    _case(
        "TC-SEM-CALL-0113",
        "§8.8.2 实参先于接收变量类型：实参合法但 var 类型与返回类型不符 -> "
        "TYPE_MISMATCH（调用点）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int64 = @HERE@call lib.one, 1\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    _case(
        "TC-SEM-CALL-0114",
        "§8.8.2 调用安全先于接收变量类型；@fast 目标 + 错误接收类型 -> FAST_CALL",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int64 = @HERE@call lib.ff, 1\n",
         "lib.tc": LIB +
         "public @fast func ff(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0115",
        "§8.4/§6.2.6 call_indirect 只按位置；命名实参 -> UNKNOWN_ARGUMENT",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var fv: fnptr<int32, int32> = funcaddr lib.f\n"
         "unsafe then\n"
         "    var r: int32 = call_indirect fv, @HERE@a: 1\n"
         "end\n",
         "lib.tc": LIB +
         "public func f(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_UNKNOWN_ARGUMENT",
    ),
    _case(
        "TC-SEM-CALL-0116",
        "§9.2 call_indirect 在安全代码中是 unsafe 指令 -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var fv: fnptr<void> = copy fnptr<void>, nullptr\n"
         "_ = @HERE@call_indirect fv\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-CALL-0117",
        "§6.2.6 call_indirect 的 fp 必须是 fnptr；非 fnptr -> TYPE_MISMATCH，"
        "先于安全上下文检查",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "_ = call_indirect @HERE@p\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    _case(
        "TC-SEM-CALL-0126",
        "§8.8.2 调用安全先于调用点结果形态：@fast void 调用写在 var 初始化器"
        " -> FAST_CALL，不报 VOID",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int32 = @HERE@call lib.fv\n",
         "lib.tc": LIB +
         "public @fast func fv() void then\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0127",
        "§6.14/§8.8.2 有结果调用用 `_` 丢弃结果仍做完整实参类型检查 -> "
        "LITERAL_TYPE",
        {"root.tc": PROGRAM +
         "import lib\n"
         "_ = call lib.one, @HERE@true\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_LITERAL_TYPE",
    ),
    _case(
        "TC-SEM-CALL-0120",
        "§8.8.2 实参检查先于接收变量类型：实参字面量错误 -> LITERAL_TYPE，"
        "不报结果类型 TYPE_MISMATCH",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var x: int64 = call lib.one, @HERE@true\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_LITERAL_TYPE",
    ),
    _case(
        "TC-SEM-CALL-0121",
        "§8.8.2/§9.8 @safe 函数中 call_indirect 先按调用安全报 "
        "SAFE_VIOLATION，不报命名实参 UNKNOWN_ARGUMENT",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"
         "public @safe func f(p: fnptr<int32, int32>) int32 then\n"
         "    var x: int32 = @HERE@call_indirect p, a: 1\n"
         "    _ = return x\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-CALL-0122",
        "§8.8.2 call_indirect 实参先于接收变量类型；实参合法但接收类型不符 "
        "-> TYPE_MISMATCH（调用点）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var fp: fnptr<int32, int32> = funcaddr lib.one\n"
         "unsafe then\n"
         "    var x: int64 = @HERE@call_indirect fp, 1\n"
         "end\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    _case(
        "TC-SEM-CALL-0123",
        "§8.8.2 call_indirect 返回 void 时只能写 `_`；作为 var 初始化器 -> "
        "VOID（调用助记符）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var vf: fnptr<void> = funcaddr lib.v\n"
         "unsafe then\n"
         "    var x: int32 = @HERE@call_indirect vf\n"
         "end\n",
         "lib.tc": LIB +
         "public func v() void then\n"
         "    _ = return\n"
         "end\n"},
        "SEM", "TC_CE_VOID",
    ),
    _case(
        "TC-SEM-CALL-0124",
        "§8.8.2 调用安全先于调用点 LHS：const 目标 + @fast 调用 -> FAST_CALL",
        {"root.tc": PROGRAM +
         "import lib\n"
         "const K: int32 = 1\n"
         "K = @HERE@call lib.ff\n",
         "lib.tc": LIB +
         "public @fast func ff() int32 then\n"
         "    _ = return 0\n"
         "end\n"},
        "SEM", "TC_CE_FAST_CALL",
    ),
    _case(
        "TC-SEM-CALL-0125",
        "§5.7.3/§8.8.2 调用安全通过后先检查 LHS；对 const 赋值 -> "
        "CONST_ASSIGNMENT 先于实参类型",
        {"root.tc": PROGRAM +
         "import lib\n"
         "const K: int32 = 1\n"
         "@HERE@K = call lib.one, true\n",
         "lib.tc": LIB +
         "public func one(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_CONST_ASSIGNMENT",
    ),
    # ------------------------------------------------------------------
    # Safety model (§9).
    _case(
        "TC-SEM-SAFE-0101",
        "§9.2/§9.8 安全代码中的 load -> UNSAFE_OPERATION（助记符）",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var x: int32 = @HERE@load int32, p\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0102",
        "§9.2/§9.8 安全代码中的 store -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "_ = @HERE@store int32, p, 1\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0103",
        "§9.2/§9.8 安全代码中的 memmove -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "_ = @HERE@memmove int32, p, p, 1\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0104",
        "§9.2/§9.8 安全代码中的 ptradd -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var q: ptr<int32> = @HERE@ptradd int32, p, 1\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0105",
        "§9.2/§9.8 安全代码中的 ptrsub -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var q: ptr<int32> = @HERE@ptrsub int32, p, 1\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0106",
        "§9.2/§9.8 ptr->int 的 bitcast 是 unsafe -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var x: int64 = @HERE@bitcast int64, p\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0107",
        "§9.2/§9.8 int->ptr 的 bitcast 是 unsafe -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var x: int64 = copy int64, 0\n"
         "var p: ptr<int32> = @HERE@bitcast ptr<int32>, x\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0108",
        "§9.2/§9.8 fnptr->int 的 bitcast 是 unsafe -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var f: fnptr<void> = copy fnptr<void>, nullptr\n"
         "var x: int64 = @HERE@bitcast int64, f\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0117",
        "§12.4 不可达代码不参与 DI/末尾传播，但仍必须通过安全/构造检查；"
        "不可达 safe load -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "_ = br E\n"
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var x: int32 = @HERE@load int32, p\n"
         "label E:\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0115",
        "§9.2/§9.8 int->fnptr 的 bitcast 是 unsafe -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var x: int64 = copy int64, 0\n"
         "var f: fnptr<void> = @HERE@bitcast fnptr<void>, x\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0116",
        "§9.3/§9.8 @safe 函数中的 ptr->int bitcast -> SAFE_VIOLATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe func f(p: ptr<int32>) int64 then\n"
         "    var x: int64 = @HERE@bitcast int64, p\n"
         "    _ = return x\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-SAFE-0109",
        "§9.3/§9.8 @safe 函数体内的 unsafe 指令 -> SAFE_VIOLATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe func f(p: ptr<int32>) int32 then\n"
         "    var x: int32 = @HERE@load int32, p\n"
         "    _ = return x\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-SAFE-0110",
        "§9.3/§9.8 @safe 函数不得含任何 unsafe 块（含空块）-> SAFE_VIOLATION；"
        "位置为 unsafe 关键字",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe func f() void then\n"
         "    @HERE@unsafe then\n"
         "    end\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-SAFE-0111",
        "§9.3/§9.8 @safe 函数中的 unsafe 块（块内即使合法）仍是 "
        "SAFE_VIOLATION；位置为 unsafe 关键字",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe func f(p: ptr<int32>) void then\n"
         "    @HERE@unsafe then\n"
         "        _ = store int32, p, 1\n"
         "    end\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-SAFE-0112",
        "§9.2/§9.8 store.volatile 也是 unsafe 指令 -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "_ = @HERE@store.volatile int32, p, 1\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0113",
        "§9.2/§9.8 load.volatile 也是 unsafe 指令 -> UNSAFE_OPERATION",
        {"root.tc": PROGRAM +
         "var p: ptr<int32> = copy ptr<int32>, nullptr\n"
         "var x: int32 = @HERE@load.volatile int32, p\n"},
        "SEM", "TC_CE_UNSAFE_OPERATION",
    ),
    _case(
        "TC-SEM-SAFE-0114",
        "§8.6/§9.6 @safe 函数中调用 @fast 函数 -> SAFE_VIOLATION 优先于 "
        "FAST_CALL",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func ff() void then\n"
         "end\n"
         "public @safe func f() void then\n"
         "    _ = @HERE@call Self.ff\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
    _case(
        "TC-SEM-MODE-0104",
        "§10.9 .nsw/.nuw 类型/符号不匹配 -> MODE，先于“合法 base 但非 @fast”"
        "的 ANNOTATION；§12.5.1 后缀 Token",
        {"root.tc": PROGRAM +
         "var x: uint32 = add.@HERE@nsw uint32, 1, 2\n"},
        "SEM", "TC_CE_MODE",
    ),
    _case(
        "TC-SEM-MODE-0105",
        "§10.9 .nsw/.nuw 只用于整数；@fast 内浮点 -> MODE",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(x: float64) float64 then\n"
         "    var y: float64 = add.@HERE@nuw float64, x, 1.0\n"
         "    _ = return y\n"
         "end\n"},
        "SEM", "TC_CE_MODE",
    ),
    # ------------------------------------------------------------------
    # Annotation matrix (§2.6/§8.6).
    _case(
        "TC-SEM-ANN-0201",
        "§8.6 未知函数注解 -> ANNOTATION（注解 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@unknown func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0202",
        "§8.6 函数注解不接受参数 -> ANNOTATION（注解 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@inline(1) func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0203",
        "§8.6 重复注解 -> ANNOTATION；位置为第二次注解 Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast @HERE@@fast func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0204",
        "§8.6 @unsafe/@safe/@fast 互斥 -> ANNOTATION（冲突的第二个注解）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe @HERE@@fast func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0205",
        "§8.6 @fast_math 必须与 @fast 同用 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@fast_math func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0206",
        "§8.6 @pure 只在 @fast 函数上合法 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@pure func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0207",
        "§8.6 @inline/@noinline 互斥 -> ANNOTATION（第二个注解）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @inline @HERE@@noinline func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0208",
        "§8.6 @nonnull 只在 @fast 指针形参上合法；非 @fast 函数 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(@HERE@@nonnull p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0209",
        "§8.6 @nonnull 只用于指针形参；整数形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@nonnull n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0210",
        "§8.6 @align 只用于指针形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@align(8) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0211",
        "§8.6 @align(N) 的 N 必须是正的 2 的幂；0 -> ANNOTATION（N Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@0) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0212",
        "§8.6 @align(N) 非 2 的幂 -> ANNOTATION（N Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@3) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0213",
        "§8.6/§13.2.2 @align N 按 W 位地址宽度；32 位下 2^32 不可表示 -> "
        "ANNOTATION（N Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@4294967296) p: ptr<int32>) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION", profile="w32",
    ),
    _case(
        "TC-SEM-ANN-0214",
        "§8.6 @range 必须 lo <= hi -> ANNOTATION（hi Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@range(10, @HERE@1) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0215",
        "§8.6 @range 只用于整数形参；浮点形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@range(0, 1) x: float64) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0216",
        "§8.6 @range 参数个数非法 -> ANNOTATION（注解 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@range(0) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0217",
        "§8.6 字面量无法定型为形参类型时 LITERAL_RANGE 优先于 ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@range(0, @HERE@128) n: int8) void then\n"
         "end\n"},
        "SEM", "TC_CE_LITERAL_RANGE",
    ),
    _case(
        "TC-SEM-ANN-0218",
        "§8.6 @range 的 lo/hi 必须是整数常量；浮点常量路径 -> ANNOTATION"
        "（常量 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float64 = 1.0\n"
         "public @fast func f(@range(@HERE@Self.F, Self.F) n: int32) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0219",
        "§8.6 @range 的 const 常量必须与形参同型；不同型 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const L: int64 = 1\n"
         "public @fast func f(@range(@HERE@Self.L, Self.L) n: int32) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0220",
        "§8.6 @range 的 const 路径未解析 -> UNDEFINED_NAME",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@range(Self.@HERE@Missing, 1) n: int32) void "
         "then\nend\n"},
        "SEM", "TC_CE_UNDEFINED_NAME",
    ),
    _case(
        "TC-SEM-ANN-0221",
        "§8.6 形参注解重复 -> ANNOTATION（第二个注解 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@nonnull @HERE@@nonnull p: ptr<int32>) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0222",
        "§2.6/§14.3 字段位置只允许 @padding；其它注解 -> ANNOTATION（注解 Token）",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var x: int32 @HERE@@align(4)\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0223",
        "§8.6 未知形参注解 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@unknown(1) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0224",
        "§8.6 @readonly 只在 @fast 函数上合法 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@readonly func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0225",
        "§8.6 @align(N) 的 N 必须是正整数；负号 -> ANNOTATION（N Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@-1) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0226",
        "§8.6 @willreturn 只在 @fast 函数上合法 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@willreturn func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0227",
        "§8.6 @no_overflow 只在 @fast 函数上合法 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@no_overflow func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0228",
        "§8.6 @no_alias 只用于指针形参；整数形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@no_alias n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0229",
        "§2.6/§8.6 @padding 只是字段注解；用于函数 -> ANNOTATION（注解 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@padding(4) func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0230",
        "§8.6 @safe 只用于函数；用于形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(@HERE@@safe p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0231",
        "§8.6 @unsafe 只用于函数；用于形参 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(@HERE@@unsafe p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0233",
        "§8.6/§4.6 @range 的 m.K 必须是 public const；导入 private const "
        "-> PRIVATE_ACCESS，位置为成员名并关联声明",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB + "import base\n"
         "public @fast func f(@range(base.@HERE@K, base.K) n: int32) void "
         "then\nend\n",
         "base.tc": LIB + "private const @REL@K: int32 = 1\n"},
        "SEM", "TC_CE_PRIVATE_ACCESS",
    ),
    _case(
        "TC-SEM-ANN-0232",
        "§8.6 @nounwind 只在 @fast 函数上合法 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @HERE@@nounwind func f() void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0234",
        "§8.6 @align 的 const 路径端点值为 0 -> ANNOTATION（端点 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const ZERO: int32 = 0\n"
         "public @fast func f(@align(@HERE@Self.ZERO) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0235",
        "§8.6 @align 的 const 路径端点值非 2 的幂 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const THREE: int32 = 3\n"
         "public @fast func f(@align(@HERE@Self.THREE) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0236",
        "§8.6 @align 的 const 路径端点值超出 w32 地址宽度 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const BIG: uint64 = 4294967296\n"
         "public @fast func f(@align(@HERE@Self.BIG) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION", profile="w32",
    ),
    _case(
        "TC-SEM-ANN-0237",
        "§8.6 @range 的 const 路径端点 lo > hi -> ANNOTATION（hi Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const HI: int32 = 10\n"
         "public const LO: int32 = 1\n"
         "public @fast func f(@range(Self.HI, @HERE@Self.LO) n: int32) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0238",
        "§8.6 @align 的 const 路径端点由整数纯表达式产生；SEM 求值后非 2 的幂 "
        "-> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const THREE: int32 = add int32, 1, 2\n"
         "public @fast func f(@align(@HERE@Self.THREE) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-CFG-0022",
        "§12.2 模块内源序：非 void 函数的重复标签与 MISSING_RETURN 并存时，"
        "返回类型处的 MISSING_RETURN 是更早首诊",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "    label L:\n"
         "    label L:\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0023",
        "§12.2 模块内源序：非 void 函数的畸形 br_if 与 MISSING_RETURN 并存时，"
        "保守 fallthrough 使返回类型处 MISSING_RETURN 更早",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "    _ = br_if true\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0024",
        "§5.4/§12.2 重复标签不再抑制 DI：更早的 UNINITIALIZED 优先于 "
        "DUPLICATE_LABEL",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(c: bool) int32 then\n"
         "    _ = br_if c, A\n"
         "    var x: int32 = copy int32, 1\n"
         "    label A:\n"
         "    _ = write int32, @HERE@x\n"
         "    label A:\n"
         "    _ = return 1\n"
         "end\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-ANN-0239",
        "§3.10/§8.6 比较表达式产生 bool、cast 为整数后作为 @align 端点；"
        "X=0 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const B: bool = eq int32, 1, 2\n"
         "public const X: int32 = cast int32, Self.B\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0240",
        "§3.10/§8.6 const 结构体字段读取作为 @align 端点；Z.x=0 -> "
        "ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n"
         "    var x: int32\n"
         "end\n"
         "public const Z: S = make S, x: 0\n"
         "public const X: int32 = Z.x\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0241",
        "§3.10/§8.6 比较表达式得到 H=0，@range(1,H) -> ANNOTATION（hi）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const B: bool = eq int32, 1, 2\n"
         "public const H: int32 = cast int32, Self.B\n"
         "public const L: int32 = 1\n"
         "public @fast func f(@range(Self.L, @HERE@Self.H) n: int32) void "
         "then\nend\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-CFG-0029",
        "§12.2 缺失标签的 br 不再全局抑制 DI；已知边上的第 6 行 "
        "UNINITIALIZED 早于第 7 行 LABEL_NOT_FOUND",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(c: bool) void then\n"
         "    _ = br_if c, A\n"
         "    var x: int32 = copy int32, 1\n"
         "    label A:\n"
         "    _ = write int32, @HERE@x\n"
         "    _ = br Missing\n"
         "    label L:\n"
         "end\n"},
        "SEM", "TC_CE_UNINITIALIZED",
    ),
    _case(
        "TC-SEM-ANN-0242",
        "§6.3 uint32 加法回绕：UINT32_MAX+1=0；@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: uint32 = add uint32, 4294967295, 1\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0243",
        "§6.3 shl 的 k>=n：a=0 -> 0；@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = shl int32, 0, 40\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0244",
        "§6.3 shr 的 k>=n -> 0；@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = shr int32, 123, 40\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0245",
        "§6.3 mod(INT_MIN,-1)=0 不 trap；@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = mod int32, -2147483648, -1\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0246",
        "§6.3 uint64 abs 保持原值；max 非 2 的幂 -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: uint64 = abs uint64, 18446744073709551615\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0247",
        "§6.3.3 float32 mod b=inf 按位等于 a；R=1，@range(2,1) -> "
        "ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const R: float32 = mod float32, 1.0f, inf\n"
         "public const X: int32 = cast int32, Self.R\n"
         "public @fast func f(@range(2, @HERE@Self.X) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0248",
        "§6.3 inf+1=inf 合法；比较得到 1，@range(2,1) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const I: float32 = inf\n"
         "public const S: float32 = add float32, Self.I, 1.0f\n"
         "public const B: bool = eq float32, Self.S, inf\n"
         "public const X: int32 = cast int32, Self.B\n"
         "public @fast func f(@range(2, @HERE@Self.X) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0249",
        "§6.3.2 精确非规格化结果不是 strict 下溢；R=2^-1023 精确，"
        "cast int32 得 0，@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const N: float64 = 2.2250738585072014e-308\n"
         "public const R: float64 = mul float64, Self.N, 0.5\n"
         "public const X: int32 = cast int32, Self.R\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0250",
        "§6.3.2 f32 精确非规格化结果保留值；cast int32 得 0，"
        "@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const N: float32 = 1.1754944e-38f\n"
         "public const R: float32 = mul float32, Self.N, 0.5f\n"
         "public const X: int32 = cast int32, Self.R\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0251",
        "§6.3.1 inf/0 是合法 IEEE 结果 inf（非除零/无效）；比较得到 1，"
        "@range(2,1) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const I: float32 = inf\n"
         "public const Q: float32 = div float32, Self.I, 0.0f\n"
         "public const B: bool = eq float32, Self.Q, inf\n"
         "public const X: int32 = cast int32, Self.B\n"
         "public @fast func f(@range(2, @HERE@Self.X) n: int32) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0252",
        "§6.6.1 有符号加宽保留符号：int8 -128 -> int64 -128；"
        "@align(负数) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const M: int8 = -128\n"
         "public const X: int64 = cast int64, Self.M\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0253",
        "§13.2.2 w32 isize -1 -> int64 必须为 -1；@range(0,-1) -> "
        "ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const M: isize = -1\n"
         "public const X: int64 = cast int64, Self.M\n"
         "public @fast func f(@range(0, @HERE@Self.X) n: int64) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION", profile="w32",
    ),
    _case(
        "TC-SEM-ANN-0254",
        "§6.6.3 w32 bitcast int32 字面量源位宽=目标位宽；0 -> @align(0) "
        "ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = bitcast int32, 0\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION", profile="w32",
    ),
    _case(
        "TC-SEM-ANN-0255",
        "§6.6.3 w64 bitcast uint32 字面量源位宽=目标位宽；0 -> "
        "@align(0) ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: uint32 = bitcast uint32, 0\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0256",
        "§6.3.2 精确非规格化 DIV（min_sub/1.0）保留值；cast 得 0，"
        "@align(0) -> ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const N: float32 = 1.40129846e-45f\n"
         "public const R: float32 = div float32, Self.N, 1.0f\n"
         "public const X: int32 = cast int32, Self.R\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0257",
        "§6.3.1 有限非零 / inf 为精确 +0；@align(0) -> ANNOTATION（f32）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float32 = div float32, 1.0f, inf\n"
         "public const X: int32 = cast int32, Self.F\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0258",
        "§6.3.1 有限非零 / inf 为精确 +0；@align(0) -> ANNOTATION（f64）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float64 = div float64, 1.0, inf\n"
         "public const X: int32 = cast int32, Self.F\n"
         "public @fast func f(@align(@HERE@Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0259",
        "§12.2 #3 same-parameter annotation N error precedes the later ptr<void> "
        "type error",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@3) p: ptr<void>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0260",
        "§12.2 #3 invalid @align placement precedes the later void parameter "
        "type error",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@align(3) p: void) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0261",
        "§8.6/§12.2 #3 a valid @align does not mask the later ptr<void> type error",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(4) p: ptr<@HERE@void>) void then\n"
         "end\n"},
        "SEM", "TC_CE_VOID",
    ),
    _case(
        "TC-SEM-ANN-0262",
        "§12.2 #3 invalid @nonnull arity precedes the later void parameter type error",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@nonnull(1) p: void) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0263",
        "§12.2 #3 non-integer @range placement precedes the later void parameter type error",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@range(1, 2) p: void) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0264",
        "§8.6/§12.5.1 bare unresolved NAME is syntactically a struct: @align "
        "placement error wins at the annotation Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@align(3) p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0265",
        "§8.6/§12.5.1 defined struct control: @align placement error at the "
        "annotation Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct Nope then\n"
         "    readonly x: int32\n"
         "end\n"
         "public @fast func f(@HERE@@align(3) p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0266",
        "§8.6/§12.5.1 bare unresolved NAME with a legal @align N still fails "
        "placement before the undefined type",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@align(4) p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0267",
        "§8.6/§12.5.1 @nonnull on a bare unresolved NAME is a placement "
        "error at the annotation Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@nonnull p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0268",
        "§8.6/§12.5.1 @range on a bare unresolved NAME is a placement error "
        "at the annotation Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@range(1, 2) p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0269",
        "§8.6/§12.5.1 syntactic pointer control: @align N error at the N Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(@HERE@3) p: ptr<Nope>) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-ANN-0270",
        "§8.6/§12.5.1 @no_alias on a bare unresolved NAME is a placement "
        "error at the annotation Token",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@HERE@@no_alias p: Nope) void then\n"
         "end\n"},
        "SEM", "TC_CE_ANNOTATION",
    ),
    _case(
        "TC-SEM-CONST-0004",
        "§3.10 正向允许集外的 copy 出现在 const 初始化器 -> CONST_FORM"
        "（操作码 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB + "public const X: int32 = @HERE@copy int32, 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0005",
        "§3.10 const 初始化器禁止 call -> CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB + "public const X: int32 = @HERE@call Self.g\n"
         "public func g() int32 then\n    _ = return 1\nend\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0006",
        "§3.10 const 初始化器禁止 load -> CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const P: ptr<int32> = nullptr\n"
         "public const X: int32 = @HERE@load int32, Self.P\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0007",
        "§3.10 const 初始化器禁止 read -> CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB + "public const X: int32 = @HERE@read int32\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0008",
        "§3.10 const_expr 的 op 带模式后缀 -> CONST_FORM（后缀 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = add.@HERE@wrap int32, 1, 2\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0009",
        "§3.10 const 中指针 bitcast -> CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const P: ptr<int32> = @HERE@bitcast ptr<int32>, 0\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0010",
        "§3.10/§4.7 static var 初始化器必须是 const_expr；copy -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public static var X: int32 = @HERE@copy int32, 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0011",
        "§3.10 局部 const 初始化器同样只接受 const_expr；copy -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    const X: int32 = @HERE@copy int32, 1\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0012",
        "§3.10 const 中指针值→整数 bitcast 仍属指针位重解释 -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const P: ptr<int32> = nullptr\n"
         "public const X: int64 = @HERE@bitcast int64, Self.P\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0013",
        "§3.10 const_expr 禁止 var 值引用；局部 const = 局部 var -> "
        "CONST_FORM（目标 Token）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    var v: int32 = copy int32, 1\n"
         "    const D: int32 = @HERE@v\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0014",
        "§3.10 const_expr 禁止参数值引用；局部 const = 参数 -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(p: int32) void then\n"
         "    const D: int32 = @HERE@p\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0015",
        "§3.10 make 字段值必须是 const；字段引用局部 var -> CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public func f() void then\n"
         "    var v: int32 = copy int32, 1\n"
         "    const C: S = make S, x: @HERE@v\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0016",
        "§3.10/§4.7 static var 初始化器禁止引用 static var（Self.A）-> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public static var A: int32 = 1\n"
         "public static var B: int32 = Self.@HERE@A\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0017",
        "§3.10/§4.7 const 初始化器禁止引用 static var（Self.A）-> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public static var A: int32 = 1\n"
         "public const C: int32 = Self.@HERE@A\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0018",
        "§3.10/§4.10 跨模块 const 初始化器禁止引用 public static var -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import base\n"
         "public const C: int32 = base.@HERE@V\n",
         "base.tc": LIB + "public static var V: int32 = 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0019",
        "§3.10 const 初始化器禁止裸名引用本模块 static var -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public static var A: int32 = 1\n"
         "public const C: int32 = @HERE@A\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0020",
        "§3.10 转换源名字仍须是 const；bitcast 源引用局部 var -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    var v: int32 = copy int32, 1\n"
         "    const X: int32 = bitcast int32, @HERE@v\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0021",
        "§3.10 转换源名字仍须是 const；cast 源引用模块 static var -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public static var A: int32 = 1\n"
         "public const X: int64 = cast int64, Self.@HERE@A\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0022",
        "§3.10 转换源名字仍须是 const；trunc 源引用局部 var -> "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    var v: int32 = copy int32, 1\n"
         "    const X: int16 = trunc int16, @HERE@v\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0023",
        "§3.10/§4.10 转换源引用跨模块 public static var -> CONST_FORM",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import base\n"
         "public const X: int64 = cast int64, base.@HERE@V\n",
         "base.tc": LIB + "public static var V: int32 = 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0024",
        "§12.2 源序：base.V.x 同时有 static var 基名与字段错误；"
        "CONST_FORM@V 先于 TYPE_MISMATCH@x",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import base\n"
         "public const A: int32 = base.@HERE@V.x\n",
         "base.tc": LIB + "public static var V: int32 = 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0025",
        "§12.2 源序：v.x 同时有局部 var 基名与字段错误；"
        "CONST_FORM@v 先于 TYPE_MISMATCH@x",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    var v: int32 = copy int32, 1\n"
         "    const A: int32 = @HERE@v.x\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CONST-0026",
        "§3.10/§12.2 static var 结构体字段链的基名仍先报 "
        "CONST_FORM@V",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import base\n"
         "public const A: int32 = base.@HERE@V.x\n",
         "base.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public static var V: S = make S, x: 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-SEM-CFG-0027",
        "§12.2 缺失标签的 br_if 仍有已知 fallthrough 边；返回类型处 "
        "MISSING_RETURN 优先于 LABEL_NOT_FOUND",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f(c: bool) @HERE@int32 then\n"
         "    _ = br_if c, M\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-CFG-0028",
        "§12.2 缺失标签的 br 无已知出口；无法证明末尾可达时仍报 "
        "LABEL_NOT_FOUND",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    _ = br @HERE@M\n"
         "end\n"},
        "SEM", "TC_CE_LABEL_NOT_FOUND",
    ),
    _case(
        "TC-SEM-CFG-0025",
        "§7.2/§12.3 无 MISSING_RETURN 竞争时，重复标签仍报 DUPLICATE_LABEL",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    label @REL@L:\n"
         "    label @HERE@L:\n"
         "end\n"},
        "SEM", "TC_CE_DUPLICATE_LABEL",
    ),
    _case(
        "TC-SEM-CFG-0026",
        "§6.15.1 无 MISSING_RETURN 竞争时，畸形 br_if 仍报 OPERAND_COUNT",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    _ = @HERE@br_if true\n"
         "end\n"},
        "SEM", "TC_CE_OPERAND_COUNT",
    ),
    _case(
        "TC-SEM-CFG-0030",
        "§7.7.1 switch 不得因常量选择器删除边：恒 0 选择器仍保留默认标签边，"
        "默认标签落到 end 的非 void 函数 -> MISSING_RETURN",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() @HERE@int32 then\n"
         "    _ = switch 0, A, D\n"
         "    label A:\n"
         "    _ = br A\n"
         "    label D:\n"
         "@REL@end\n"},
        "SEM", "TC_CE_MISSING_RETURN",
    ),
    _case(
        "TC-SEM-MODE-0106",
        "§6.15.1 add.wrap 已定义组合但浮点操作数不适用 -> MODE",
        {"root.tc": PROGRAM +
         "var x: float64 = add.@HERE@wrap float64, 1.0, 2.0\n"},
        "SEM", "TC_CE_MODE",
    ),
    _case(
        "TC-SEM-MODE-0107",
        "§6.3 neg.wrap 只定义于有符号整数；uint32 -> MODE",
        {"root.tc": PROGRAM +
         "var x: uint32 = neg.@HERE@wrap uint32, 1\n"},
        "SEM", "TC_CE_MODE",
    ),
    _case(
        "TC-SEM-MODE-0108",
        "§6.3 neg.wrap 只定义于有符号整数；float64 -> MODE",
        {"root.tc": PROGRAM +
         "var x: float64 = neg.@HERE@wrap float64, 1.0\n"},
        "SEM", "TC_CE_MODE",
    ),
    _case(
        "TC-SEM-MODE-0109",
        "§6.15.1 mul.ieee 已定义组合但整数操作数不适用 -> MODE",
        {"root.tc": PROGRAM +
         "var x: int32 = mul.@HERE@ieee int32, 1, 2\n"},
        "SEM", "TC_CE_MODE",
    ),
    _case(
        "TC-SEM-CALL-0128",
        "§8.8.2/#10 未知实参先于缺失实参 -> UNKNOWN_ARGUMENT",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var r: int32 = call lib.f, @HERE@x: 1\n",
         "lib.tc": LIB +
         "public func f(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_UNKNOWN_ARGUMENT",
    ),
    _case(
        "TC-SEM-CALL-0129",
        "§8.8.2/#10 命名实参顺序先于实参类型：顺序不符 + 字面量类型错 -> "
        "ARGUMENT_ORDER",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var r: int32 = call lib.f, @HERE@b: true, a: 2\n",
         "lib.tc": LIB +
         "public func f(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_ARGUMENT_ORDER",
    ),
    _case(
        "TC-SEM-CALL-0130",
        "§3.7/#10 make 未知字段先于缺失字段 -> UNKNOWN_FIELD",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var a: int32\n"
         "    var b: int32\n"
         "end\n"
         "var s: S = make S, @HERE@z: 1\n"},
        "SEM", "TC_CE_UNKNOWN_FIELD",
    ),
    _case(
        "TC-SEM-CALL-0131",
        "§3.7/#10 make 重复字段先于未知字段 -> DUPLICATE_FIELD",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var a: int32\n"
         "    var b: int32\n"
         "end\n"
         "var s: S = make S, @REL@a: 1, @HERE@a: 2, z: 3\n"},
        "SEM", "TC_CE_DUPLICATE_FIELD",
    ),
    _case(
        "TC-SEM-CALL-0132",
        "§3.7/#10 make 缺失字段先于顺序 -> MISSING_FIELD",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var a: int32\n"
         "    var b: int32\n"
         "    var c: int32\n"
         "end\n"
         "var s: S = make @HERE@S, c: 1, b: 2\n"},
        "SEM", "TC_CE_MISSING_FIELD",
    ),
    _case(
        "TC-SEM-CALL-0133",
        "§8.8.2 调用安全先于实参检查：安全代码调用 @unsafe 目标 + 未知命名实参 "
        "-> UNSAFE_CALL",
        {"root.tc": PROGRAM + "import lib\n"
         "var r: int32 = @HERE@call lib.f, x: 1\n",
         "lib.tc": LIB +
         "public @unsafe func f(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"},
        "SEM", "TC_CE_UNSAFE_CALL",
    ),
    _case(
        "TC-SEM-CALL-0134",
        "§3.7/#10 make 命名字段顺序先于字段值类型 -> FIELD_ORDER",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var a: int32\n"
         "    var b: int32\n"
         "end\n"
         "var s: S = make S, @HERE@b: 2, a: true\n"},
        "SEM", "TC_CE_FIELD_ORDER",
    ),
    _case(
        "TC-SEM-CALL-0135",
        "§12.2 #3 同行多个未知 make 字段按列号升序取最左者 -> UNKNOWN_FIELD",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var a: int32\n"
         "end\n"
         "var s: S = make S, @HERE@z: 1, w: 2\n"},
        "SEM", "TC_CE_UNKNOWN_FIELD",
    ),
    _case(
        "TC-SEM-DI-0010",
        "§12.2 #2 跨模块按 import 源序深度优先：a.tc 第 5 行的类型错误先于 "
        "b.tc 第 2 行的重复形参，即使后者源行更早",
        {"root.tc": PROGRAM + "import a\nimport b\n",
         "a.tc": LIB +
         "public func f() int32 then\n"
         "    var v: bool = copy bool, true\n"
         "    var t: bool = copy bool, false\n"
         "    var w: int32 = copy int32, @HERE@v\n"
         "    _ = return w\n"
         "end\n",
         "b.tc": LIB +
         "public func g(u: int32, u: int32) int32 then\n"
         "    _ = return u\n"
         "end\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    _case(
        "TC-SEM-DI-0011",
        "§7.7.2 不可达代码（无前驱块）不参与 DI，但仍必须通过名称检查：不可达"
        "块内读取未定义名 -> UNDEFINED_NAME",
        {"root.tc": PROGRAM +
         "var c: bool = copy bool, false\n"
         "_ = br_if c, A\n"
         "_ = br M\n"
         "label A:\n"
         "var x: int32 = copy int32, 1\n"
         "_ = br M\n"
         "label M:\n"
         "_ = br E\n"
         "label DEAD:\n"
         "_ = write int32, @HERE@noSuchName\n"
         "label E:\n"
         "_ = writeln int32, 9\n"},
        "SEM", "TC_CE_UNDEFINED_NAME",
    ),
    _case(
        "TC-SEM-SAFE-0120",
        "§9.3/§9.8 @safe 函数体内出现任何 unsafe 块（含空块）-> SAFE_VIOLATION；"
        "§12.5.1 定位到 unsafe 关键字",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @safe func f() int32 then\n"
         "    @HERE@unsafe then\n"
         "    end\n"
         "    var r: int32 = copy int32, 0\n"
         "    _ = return r\n"
         "end\n"},
        "SEM", "TC_CE_SAFE_VIOLATION",
    ),
]


POSITIVE = [
    _positive(
        "TC-TOOL-SEM-0101",
        "§5.4/§7.7 DI 路径语义正例：顺序、分支、循环内 var 重初始化、字段读取、"
        "addr 未初始化存储合法",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var f: int32\n"
         "end\n"
         "var s: S = make S, 1\n"
         "var x: int32 = copy int32, 2\n"
         "label L:\n"
         "var y: int32 = copy int32, x\n"
         "_ = write int32, y\n"
         "_ = br_if true, L\n"
         "label M:\n"
         "_ = write int32, s.f\n"
         "var p: ptr<int32> = addr int32, y\n"
         "var n: int32 = copy int32, 0\n"
         "unsafe then\n"
         "    n = load int32, p\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0102",
        "§5.4 var x = read T 成功后才初始化；后续读取合法（失败路径终止程序）",
        {"root.tc": PROGRAM +
         "var x: int32 = read int32\n"
         "var y: int32 = add int32, x, 1\n"
         "_ = write int32, y\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0103",
        "§7.7.2 不可达代码仍类型检查通过、不参与 DI；程序自然落到末尾正常终止",
        {"root.tc": PROGRAM +
         "_ = br E\n"
         "var x: int32 = copy int32, 1\n"
         "_ = write int32, x\n"
         "label E:\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0104",
        "§7.7.3 void 自然落到 end、trap 终止、无出口 br 循环均不报 "
        "MISSING_RETURN",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func v() void then\n"
         "    var x: int32 = copy int32, 1\n"
         "end\n"
         "public func t() int32 then\n"
         "    _ = trap\n"
         "end\n"
         "public func l() int32 then\n"
         "    label L:\n"
         "    _ = br L\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0105",
        "§7.2/§5.1 标签与值名分属不同命名空间；前后向标签均合法",
        {"root.tc": PROGRAM +
         "var L: int32 = copy int32, 1\n"
         "_ = br M\n"
         "label L:\n"
         "_ = write int32, L\n"
         "label M:\n"
         "_ = br L\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0106",
        "§8.4/§8.8.2 直接调用位置/命名实参、void 用 `_`、有结果调用 `_` 丢弃、"
        "递归允许",
        {"root.tc": PROGRAM +
         "import lib\n"
         "var a: int32 = call lib.two, 1, 2\n"
         "var b: int32 = call lib.two, a: 1, b: 2\n"
         "_ = call lib.two, 1, 2\n"
         "_ = call lib.voidf\n"
         "var c: int32 = call lib.rec, 3\n",
         "lib.tc": LIB +
         "public func two(a: int32, b: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"
         "public func voidf() void then\n"
         "    _ = return\n"
         "end\n"
         "public func rec(n: int32) int32 then\n"
         "    var x: int32 = call Self.rec, n\n"
         "    _ = return x\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0107",
        "§9.2/§9.3 unsafe 块接受全部 unsafe 指令；@unsafe/@fast 函数体也是 "
        "unsafe 上下文",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @unsafe func u(p: ptr<int32>) int32 then\n"
         "    var x: int32 = load int32, p\n"
         "    var q: ptr<int32> = ptradd int32, p, 1\n"
         "    _ = store int32, q, x\n"
         "    _ = return x\n"
         "end\n"
         "public @fast func f(p: ptr<int32>) int64 then\n"
         "    var x: int32 = load.volatile int32, p\n"
         "    _ = store.volatile int32, p, x\n"
         "    var q: ptr<int32> = ptrsub int32, p, 0\n"
         "    _ = memmove int32, q, p, 1\n"
         "    var v: ptr<int32> = bitcast ptr<int32>, p\n"
         "    var n: int64 = bitcast int64, v\n"
         "    _ = return n\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0108",
        "§9.2 非 unsafe 操作在安全代码中合法：addr、ptrdiff、指针比较、"
        "funcaddr、funceq、数值/ptr↔ptr bitcast、trap",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 0\n"
         "var p: ptr<int32> = addr int32, x\n"
         "var d: isize = ptrdiff int32, p, p\n"
         "var is_eq: bool = ptreq int32, p, p\n"
         "var q: ptr<int32> = bitcast ptr<int32>, p\n"
         "var u: uint32 = bitcast uint32, x\n"
         "var f: fnptr<void> = copy fnptr<void>, nullptr\n"
         "var g: fnptr<void> = bitcast fnptr<void>, f\n"
         "var same: bool = funceq fnptr<void>, f, g\n"
         "var n: int32 = cast int32, u\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0109",
        "§8.6 完整合法注解矩阵：@fast 承诺、@fast_math、@inline、@safe @inline、"
        "@nonnull/@no_alias/@align/@range",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast @fast_math @pure @readonly @nounwind @willreturn "
         "@no_overflow func f() void then\n"
         "end\n"
         "public @safe @inline func g() void then\n"
         "end\n"
         "public @noinline @unsafe func h() void then\n"
         "end\n"
         "public @fast func p(@nonnull @no_alias @align(8) a: ptr<int32>, "
         "@range(0, 10) n: int32) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0110",
        "§8.6 承诺内容违反是 UB，不是静态拒绝：@pure store、@nounwind trap、"
        "@no_overflow strict add、@range 形参越界值均可静态通过",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast @pure @readonly @nounwind func f(p: ptr<int32>, "
         "n: int32) int32 then\n"
         "    _ = store int32, p, n\n"
         "    var x: int32 = add int32, n, 1\n"
         "    _ = trap\n"
         "    _ = return n\n"
         "end\n"
         "public @fast @no_overflow func g(@range(0, 10) n: int32) int32 then\n"
         "    var x: int32 = add int32, n, 1\n"
         "    _ = return x\n"
         "end\n"
         "public @fast @nounwind func h(@nonnull @no_alias @align(8) "
         "p: ptr<int32>, @range(0, 10) n: int32) int32 then\n"
         "    var x: int32 = load int32, p\n"
         "    _ = store int32, p, n\n"
         "    _ = return x\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0111",
        "§8.6 @range 的 const 常量同型合法；@align 的 const 路径语法合法",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const L: int32 = 1\n"
         "public const A: int32 = 8\n"
         "public @fast func f(@range(Self.L, Self.L) n: int32, "
         "@align(Self.A) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0122",
        "§6.6.3 switch 整数字面量源类型：无后缀 int64 下界与 u 后缀 uint64 "
        "上界均为合法整数选择器",
        {"root.tc": PROGRAM +
         "_ = switch -9223372036854775808, L\n"
         "label L:\n"
         "_ = switch 18446744073709551615u, L\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0120",
        "§8.6/§13.2.2 @align(N) 按 W 位地址宽度：32 位下 2^31 是合法的 2 的幂",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(2147483648) p: ptr<int32>) void then\n"
         "end\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-SEM-0121",
        "§8.6/§13.2.2 @align(N) 按 W 位地址宽度：64 位下 2^63 是合法的 2 的幂",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public @fast func f(@align(9223372036854775808) "
         "p: ptr<int32>) void then\nend\n"},
        profile="w64",
    ),
    _positive(
        "TC-TOOL-SEM-0119",
        "§9.2 unsafe 块接受全部 unsafe 指令：load/store/volatile/memmove/"
        "ptradd/ptrsub/ptr-int/fnptr-int/call_indirect",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 1\n"
         "var p: ptr<int32> = addr int32, x\n"
         "var f: fnptr<void> = copy fnptr<void>, nullptr\n"
         "unsafe then\n"
         "    var v: int32 = load int32, p\n"
         "    _ = store int32, p, v\n"
         "    var lv: int32 = load.volatile int32, p\n"
         "    _ = store.volatile int32, p, lv\n"
         "    var q: ptr<int32> = ptradd int32, p, 0\n"
         "    var r: ptr<int32> = ptrsub int32, p, 0\n"
         "    _ = memmove int32, q, r, 1\n"
         "    var bits: int64 = bitcast int64, p\n"
         "    var back: ptr<int32> = bitcast ptr<int32>, bits\n"
         "    var fbits: int64 = bitcast int64, f\n"
         "    var ff: fnptr<void> = bitcast fnptr<void>, fbits\n"
         "    _ = call_indirect ff\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0118",
        "§7.1 每个函数/程序的标签命名空间独立；不同函数可复用同名标签",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func f() void then\n"
         "    label L:\n"
         "    _ = br L\n"
         "end\n"
         "public func g() void then\n"
         "    label L:\n"
         "    _ = br L\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0117",
        "§5.1/§5.7.1 static var 与 const 不进入 U，入口即已初始化；函数内"
        "Self.S/Self.K 与导入 public static var 读取不产生 UNINITIALIZED",
        {"root.tc": PROGRAM + "import lib\n"
         "var y: int32 = copy int32, lib.S\n",
         "lib.tc": LIB +
         "public static var S: int32 = 1\n"
         "public const K: int32 = 2\n"
         "public func f() int32 then\n"
         "    var x: int32 = add int32, Self.S, Self.K\n"
         "    _ = return x\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0116",
        "§8.6 @range/@align 的常量路径：本模块 private const 与导入 public "
        "const 均可用于同型端点；@align 取地址宽度整数常量",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB + "import base\n"
         "private const L: int32 = 1\n"
         "public @fast func f(@range(Self.L, base.H) n: int32, "
         "@align(base.A) p: ptr<int32>) void then\nend\n",
         "base.tc": LIB + "public const H: int32 = 2\n"
         "public const A: int32 = 8\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0124",
        "§8.6 const 路径端点由整数纯表达式/ sizeof 产生且值合法；"
        "@align=8、@range(4,10) 记录",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: int32 = add int32, 7, 1\n"
         "public const L: usize = sizeof int32\n"
         "public const H: usize = 10\n"
         "public @fast func f(@align(Self.A) p: ptr<int32>, "
         "@range(Self.L, Self.H) n: usize) void then\nend\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0139",
        "§6.3.2 非精确非规格化乘法属于 strict 下溢（tininess after "
        "rounding）；SEM 不得把舍入结果当合法值",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const N: float64 = 4.9406564584124654e-324\n"
         "public const R: float64 = mul float64, Self.N, 1.5\n"
         "public const X: int32 = cast int32, Self.R\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0142",
        "§3.10 合法 const_expr：sizeof/make/数值 bitcast/比较/cast 不报 "
        "CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public const A: usize = sizeof int32\n"
         "public const B: int32 = bitcast int32, 8\n"
         "public const C: int32 = add int32, 1, 2\n"
         "public const D: bool = eq int32, 1, 1\n"
         "public const E: int32 = cast int32, Self.D\n"
         "public const F: S = make S, x: 8\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0145",
        "§3.10 const 结构体字段链（Self.KS.x）合法，不报 CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public const K: int32 = 1\n"
         "public const KS: S = make S, x: Self.K\n"
         "public const A: int32 = Self.KS.x\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0144",
        "§3.10 合法转换源：const 的 cast/trunc/bitcast、const 结构体字段与 "
        "static var 由 const 初始化均接受",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public const K: int32 = 1\n"
         "public const KS: S = make S, x: Self.K\n"
         "public const A: int64 = cast int64, Self.K\n"
         "public const B: int16 = trunc int16, Self.K\n"
         "public const C: int32 = bitcast int32, Self.K\n"
         "public const D: int32 = Self.KS.x\n"
         "public static var V: int32 = Self.K\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0143",
        "§3.10 合法 const 引用：static var 由 const 初始化、make 字段用 "
        "const、局部 const 引用局部 const 均不报 CONST_FORM",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n    var x: int32\nend\n"
         "public const K: int32 = 1\n"
         "public static var V: int32 = Self.K\n"
         "public const C: S = make S, x: Self.K\n"
         "public func f() void then\n"
         "    const A: int32 = 1\n"
         "    const B: int32 = A\n"
         "    const D: S = make S, x: B\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0140",
        "§6.6.3 bitcast int32 字面量 8 的源类型为 int32；结果 8 合法",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: int32 = bitcast int32, 8\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0141",
        "§6.3.2 非精确非规格化 DIV（f32 min_sub/1.5、f64 min_sub/1.5）"
        "属于 strict 下溢；SEM 不得解析舍入结果",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const N32: float32 = 1.40129846e-45f\n"
         "public const R32: float32 = div float32, Self.N32, 1.5f\n"
         "public const X32: int32 = cast int32, Self.R32\n"
         "public const N64: float64 = 4.9406564584124654e-324\n"
         "public const R64: float64 = div float64, Self.N64, 1.5\n"
         "public const X64: int32 = cast int32, Self.R64\n"
         "public @fast func a(@align(Self.X32) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.X64) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0134",
        "§6.3 strict shl 非零 a 的大计数与无符号高位溢出属于 CT "
        "CONST_OVERFLOW；SEM 不得误报",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: int32 = shl int32, 1, 40\n"
         "public const B: uint32 = shl uint32, 4, 30\n"
         "public @fast func a(@align(Self.A) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.B) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0135",
        "§6.6.1 浮点→整数窄类型边界属于 CT CONST_CAST；SEM 不得提前报 "
        "ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: int32 = cast int32, 2147483648.0\n"
         "public const B: int32 = cast int32, -2147483649.0\n"
         "public const C: int8 = cast int8, 128.0\n"
         "public const D: int16 = cast int16, 32768.0\n"
         "public @fast func a(@align(Self.A) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.B) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func c(@align(Self.C) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func d(@align(Self.D) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0137",
        "§6.3.1 NaN 输入和 mod(inf,·) 属于 CT FLOAT_INVALID；SEM 不得误报",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: float32 = add float32, nan, 1.0f\n"
         "public const B: float32 = mod float32, inf, 2.0f\n"
         "public const XA: int32 = cast int32, Self.A\n"
         "public const XB: int32 = cast int32, Self.B\n"
         "public @fast func a(@align(Self.XA) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.XB) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0138",
        "§6.6.1 异类型浮点 NaN 规范化为 canonical quiet NaN；f32→f64 "
        "与 f64→f32 均按规范值比较并通过 @align(1)",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float32 = bitcast float32, 2143289345\n"
         "public const D: float64 = cast float64, Self.F\n"
         "public const D64: uint64 = bitcast uint64, Self.D\n"
         "public const B1: bool = eq uint64, Self.D64, 9221120237041090560\n"
         "public const X1: int32 = cast int32, Self.B1\n"
         "public const D2: float64 = bitcast float64, 9221120237041090561\n"
         "public const F2: float32 = cast float32, Self.D2\n"
         "public const F32: uint32 = bitcast uint32, Self.F2\n"
         "public const B2: bool = eq uint32, Self.F32, 2143289344\n"
         "public const X2: int32 = cast int32, Self.B2\n"
         "public @fast func a(@align(Self.X1) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.X2) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0127",
        "§6.3 uint64 abs(max)=max；@range(2,max) 合法",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const H: uint64 = 2\n"
         "public const L: uint64 = abs uint64, 18446744073709551615\n"
         "public @fast func f(@range(Self.H, Self.L) n: uint64) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0128",
        "§6.3.3 浮点 mod 使用精确有理商；R*1e7 截断为 176696，"
        "eq 比较后 @align(1) 接受",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const R: float32 = mod float32, 291731.1875f, 0.0201671552f\n"
         "public const M: float32 = mul float32, Self.R, 10000000.0f\n"
         "public const X: int32 = cast int32, Self.M\n"
         "public const B: bool = eq int32, Self.X, 176696\n"
         "public const V: int32 = cast int32, Self.B\n"
         "public @fast func f(@align(Self.V) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0129",
        "§3.10 strict 浮点下溢属于 CT；SEM 端点求值不得误报 ANNOTATION",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float64 = mul float64, 1e-308, 1e-308\n"
         "public const X: int32 = cast int32, Self.F\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0130",
        "§6.6.1 整数 cast 越界属于 CT CONST_CAST；SEM 不得截断后接受",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: int32 = cast int32, 2147483648\n"
         "public const B: int32 = cast int32, 4294967304\n"
         "public @fast func a(@align(Self.A) p: ptr<int32>) void then\n"
         "end\n"
         "public @fast func b(@align(Self.B) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0131",
        "§13.2.2 w32 isize cast 4294967304 越界属于 CT CONST_CAST",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: isize = cast isize, 4294967304\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-SEM-0132",
        "§6.6.1 float64 2^64 -> uint64 越界属于 CT CONST_CAST",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const X: uint64 = cast uint64, 18446744073709551616.0\n"
         "public @fast func f(@align(Self.X) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0133",
        "§6.6.1 float64 1e308 -> float32 溢出属于 CT CONST_CAST",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const F: float32 = cast float32, 1e308\n"
         "public const B: bool = eq float32, Self.F, inf\n"
         "public const X: int32 = cast int32, Self.B\n"
         "public @fast func f(@range(2, Self.X) n: int32) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0125",
        "§3.10 const 结构体字段读取与比较/cast 产生的合法整数端点："
        "@align=8 与 @align=1 均接受",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public struct S then\n"
         "    var x: int32\n"
         "end\n"
         "public const Z: S = make S, x: 8\n"
         "public const B: bool = eq int32, 8, 8\n"
         "public const X: int32 = Z.x\n"
         "public const Y: int32 = cast int32, Self.B\n"
         "public @fast func a(@align(Self.X) p: ptr<int32>) void then\nend\n"
         "public @fast func b(@align(Self.Y) p: ptr<int32>) void then\nend\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0126",
        "§3.10 strict 溢出属于 CT 的 CONST_OVERFLOW；SEM 端点求值返回未解析，"
        "不得产生 ANNOTATION（--sem-only 只验证 SEM 无误报）",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public const A: int32 = add int32, 2147483647, 1\n"
         "public @fast func f(@align(Self.A) p: ptr<int32>) void then\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0123",
        "§5.7.1 DI 路径语义：trap 路径终止、不是汇合前驱；仅经 var 的路径到达"
        "读取时视为已初始化",
        {"root.tc": PROGRAM +
         "var c: bool = copy bool, true\n"
         "_ = br_if c, A\n"
         "_ = trap\n"
         "label A:\n"
         "var x: int32 = copy int32, 1\n"
         "label M:\n"
         "_ = write int32, x\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0115",
        "§5.4/§5.5 addr 取未初始化存储合法、字段赋值不改外层初始化状态；"
        "经指针读取未初始化存储是运行时 UB，不是静态拒绝",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var f: int32\n"
         "end\n"
         "_ = br_if true, L\n"
         "var x: int32 = copy int32, 1\n"
         "label L:\n"
         "var p: ptr<int32> = addr int32, x\n"
         "var s: S = make S, 1\n"
         "s.f = copy int32, 2\n"
         "_ = write int32, s.f\n"
         "unsafe then\n"
         "    var v: int32 = load int32, p\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0113",
        "§9.2/§9.3 unsafe 块与 @unsafe/@fast 函数接受 call_indirect 与 "
        "fnptr↔int bitcast；fnptr 不携带形参前置条件",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func id(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"
         "public @unsafe func call_ptr(p: fnptr<int32, int32>) int32 then\n"
         "    var x: int32 = call_indirect p, 1\n"
         "    var n: int64 = bitcast int64, p\n"
         "    _ = return x\n"
         "end\n"
         "public @fast func fast_ptr(p: fnptr<int32, int32>) int32 then\n"
         "    var x: int32 = call_indirect p, 1\n"
         "    _ = return x\n"
         "end\n"
         "public func block(p: ptr<int32>) int32 then\n"
         "    var x: int32 = copy int32, 0\n"
         "    unsafe then\n"
         "        x = load int32, p\n"
         "    end\n"
         "    _ = return x\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0114",
        "§9.1/§9.7 @safe 只含安全指令/调用：addr、ptrdiff、funcaddr、funceq、"
        "数值/ptr↔ptr bitcast、read/write、trap",
        {"root.tc": PROGRAM + "import lib\n",
         "lib.tc": LIB +
         "public func g(a: int32) int32 then\n"
         "    _ = return a\n"
         "end\n"
         "public @safe func f(x: int32) int32 then\n"
         "    var p: ptr<int32> = addr int32, x\n"
         "    var d: isize = ptrdiff int32, p, p\n"
         "    var q: ptr<int32> = bitcast ptr<int32>, p\n"
         "    var n: uint32 = bitcast uint32, x\n"
         "    var fp: fnptr<int32, int32> = funcaddr Self.g\n"
         "    var same: bool = funceq fnptr<int32, int32>, fp, fp\n"
         "    _ = write uint32, n\n"
         "    _ = return x\n"
         "end\n"},
    ),
    _positive(
        "TC-TOOL-SEM-0112",
        "§6.2.8 label 声明与 switch/br_if 控制形态；默认标签为最后一个",
        {"root.tc": PROGRAM +
         "var c: int32 = copy int32, 0\n"
         "_ = switch c, L0, L1\n"
         "label L0:\n"
         "_ = br E\n"
         "label L1:\n"
         "_ = br E\n"
         "label E:\n"
         "_ = br_if true, E\n"},
    ),
    _positive(
        "TC-SEM-SAFE-0118",
        "§3.7/§6.14 整块赋值可替换含 readonly 字段的整个结构体值；同值写回合法",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    readonly x: int32\n"
         "    var y: int32\n"
         "end\n"
         "var s: S = make S, 1, 2\n"
         "var t: S = copy S, s\n"
         "s = copy S, t\n"
         "_ = writeln int32, s.y\n"},
    ),
    _positive(
        "TC-SEM-SAFE-0119",
        "§6.14 readonly 只约束其自身路径的子树；同层 var 兄弟字段仍可字段赋值",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    readonly x: int32\n"
         "    var y: int32\n"
         "end\n"
         "var s: S = make S, 1, 2\n"
         "s.y = copy int32, 3\n"
         "_ = writeln int32, s.y\n"},
    ),
]


def _write_compile_case(case, failures, check):
    case_dir = HERE / "sem4"
    fixture = case_dir / "fixtures" / case["id"].lower()
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
        if not path.exists() or path.read_text(encoding="utf-8") != stripped:
            if check:
                failures.append(f"{case['id']}: stale {path.relative_to(HERE)}")
            else:
                path.write_text(stripped, encoding="utf-8", newline="")
    if primary_file is None:
        failures.append(f"{case['id']}: missing @HERE@")
        return
    source = f"fixtures/{case['id'].lower()}/{case['source']}"
    primary = (
        f"fixtures/{case['id'].lower()}/{primary_file}",
        primary_pos[0],
        primary_pos[1],
    )
    related = None
    if related_file is not None:
        related = (
            f"fixtures/{case['id'].lower()}/{related_file}",
            related_pos[0],
            related_pos[1],
        )
    meta = dict(case)
    meta["source"] = source
    yaml = _compile_yaml(meta, primary[0], primary[1:], related)
    yaml_path = case_dir / f"{case['id'].lower()}.yaml"
    if not yaml_path.exists() or yaml_path.read_text(encoding="utf-8") != yaml:
        if check:
            failures.append(f"{case['id']}: stale {yaml_path.relative_to(HERE)}")
        else:
            yaml_path.write_text(yaml, encoding="utf-8", newline="")


def _write_positive_case(case, failures, check):
    case_dir = HERE / "accept_s4"
    fixture = case_dir / case["id"].lower()
    fixture.mkdir(parents=True, exist_ok=True)
    for name, text in case["files"].items():
        path = fixture / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            if check:
                failures.append(f"{case['id']}: stale {path.relative_to(HERE)}")
            else:
                path.write_text(text, encoding="utf-8", newline="")
    source = f"{case['id'].lower()}/{case['source']}"
    argv = ["tc-vm", "--sem-only"]
    if case.get("profile") == "w32":
        argv.append("--profile=w32")
    argv.append("@source")
    yaml = _tool_yaml(case, source, argv)
    yaml_path = case_dir / f"{case['id'].lower()}.yaml"
    if not yaml_path.exists() or yaml_path.read_text(encoding="utf-8") != yaml:
        if check:
            failures.append(f"{case['id']}: stale {yaml_path.relative_to(HERE)}")
        else:
            yaml_path.write_text(yaml, encoding="utf-8", newline="")


def _expected_artifacts():
    compile_paths = set()
    positive_paths = set()
    for case in CASES:
        fixture = HERE / "sem4" / "fixtures" / case["id"].lower()
        for name in case["files"]:
            compile_paths.add(fixture / name)
        compile_paths.add(HERE / "sem4" / f"{case['id'].lower()}.yaml")
    for case in POSITIVE:
        fixture = HERE / "accept_s4" / case["id"].lower()
        for name in case["files"]:
            positive_paths.add(fixture / name)
        positive_paths.add(HERE / "accept_s4" / f"{case['id'].lower()}.yaml")
    return compile_paths, positive_paths


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
    for case in CASES:
        _write_compile_case(case, failures, args.check)
    for case in POSITIVE:
        _write_positive_case(case, failures, args.check)
    expected_compile, expected_positive = _expected_artifacts()
    for label, expected, root in (
        ("sem4", expected_compile, HERE / "sem4"),
        ("accept_s4", expected_positive, HERE / "accept_s4"),
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
        f"s4 case table: {len(CASES)} compile, {len(POSITIVE)} tool cases"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

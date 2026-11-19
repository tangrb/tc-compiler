#!/usr/bin/env python3
"""S5 conformance case table for TC 0.0.46.

Covers compile-time evaluation (CT) of `const` and `static var`: the six CT
codes, CT ordering/attribution/caching-visible diagnostics, the SEM
`TC_CE_CONST_FORM` allowlist boundary, the CT/SEM stage boundary and the
W=32/W=64 differences that reach CT results or CT/SEM width checks.  A second
table holds the positive allowlist acceptance sources under `accept_s5/`.

Expectations are derived from docs/TC语言及编译器标准-0.0.46.md,
docs/TC原生编译器设计说明书-0.0.46.md and
docs/TC开发测试计划-0.0.46.md, never from compiler output.  `@HERE@` marks
the primary diagnostic position and `@REL@` a related position; the generator
removes the markers and computes 1-based line and Unicode-scalar columns.

Position convention for the six CT codes (`CONST_OVERFLOW`, `CONST_DIV_ZERO`,
`CONST_SHIFT`, `CONST_FLOAT_INVALID`, `CONST_FLOAT_UNDERFLOW`, `CONST_CAST`):
§12.5.1 says the primary position is the target declaration of the constant
declaration.  This table pins that to the first token of the declaration
identifier (`K` in `const K: ...`, `V` in `static var V: ...`) for both module
`const`/`static var` and function-local `const` initializers.  `CONST_FORM`
positions follow §12.5.1 literally: the first illegal subexpression opcode or
target token, or the suffix name after the dot (same convention as S4).

Run:
    python3 tests/conformance/static/s5_cases.py --write
    python3 tests/conformance/static/s5_cases.py --check
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "@HERE@"
REL = "@REL@"

PROGRAM = "#program\n"
LIB = "#lib\n"

# Root with one runtime statement; the imported module owns the const/static
# var under test.  Keeping the root non-empty also exercises a normal program
# body on the acceptance side.
ROOT_IMPORT_LIB = PROGRAM + "import lib\nvar keep: int32 = copy int32, 0\n"


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


CASES = [
    # ------------------------------------------------------------------
    # SEM const_expr allowlist boundary (§3.10): every listed form is a
    # semantic shape error, so the stage is SEM and the position is the
    # first illegal subexpression opcode/target/suffix token (§12.5.1).
    _case(
        "TC-CT-FORM-0001",
        "§3.10 正向允许集不含 var 引用；#program 顶层 const 初始化器引用程序 "
        "var -> CONST_FORM；§12.5.1 首个非法子表达式的目标 Token",
        {"root.tc": PROGRAM +
         "var v: int32 = copy int32, 1\n"
         "const D: int32 = @HERE@v\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0002",
        "§3.10 正向允许集不含参数引用；局部 const 初始化器引用形参 -> "
        "CONST_FORM；§12.5.1 目标 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public func f(p: int32) void then\n"
         "    const D: int32 = @HERE@p\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0003",
        "§3.10 正向允许集不含 addr；const 初始化器取程序 var 地址 -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM +
         "var x: int32 = copy int32, 1\n"
         "const P: ptr<int32> = @HERE@addr int32, x\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0004",
        "§3.10 正向允许集不含 load；@unsafe 函数内的局部 const 用 load 读取"
        "参数（安全上下文外不引入 UNSAFE_OPERATION 竞争）-> CONST_FORM（操作码）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public @unsafe func f(p: ptr<int32>) void then\n"
         "    const X: int32 = @HERE@load int32, p\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0005",
        "§3.10 正向允许集不含 call；#program const 调用导入函数 -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "const X: int32 = @HERE@call lib.g\n",
         "lib.tc": LIB +
         "public func g() int32 then\n"
         "    _ = return 1\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0006",
        "§3.10/§8.5 函数地址不是编译期常量；const 初始化器 funcaddr 导入函数 "
        "-> CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM +
         "import lib\n"
         "const P: fnptr<int32> = @HERE@funcaddr lib.g\n",
         "lib.tc": LIB +
         "public func g() int32 then\n"
         "    _ = return 1\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0007",
        "§3.10 正向允许集不含 read；const 初始化器 read int32 -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM +
         "const X: int32 = @HERE@read int32\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0008",
        "§3.10 const 中的 bitcast 仅限数值↔数值；指针位重解释 -> CONST_FORM；"
        "置于 @unsafe 使指针 bitcast 的安全类别先通过，形态错误唯一",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public @unsafe func f() void then\n"
         "    const P: ptr<int32> = @HERE@bitcast ptr<int32>, 0\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0009",
        "§3.10 const 中的 bitcast 仅限数值↔数值；fnptr 位重解释 -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public @unsafe func f() void then\n"
         "    const P: fnptr<int32> = @HERE@bitcast fnptr<int32>, 0\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0010",
        "§3.10 const_expr 的 op_form 不得带 .wrap；add.wrap 形态错误 -> "
        "CONST_FORM；§12.5.1 带后缀时定位到后缀 Token（与 S4 相同：点号后的名字）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const X: int32 = add.@HERE@wrap int32, 1, 2\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0011",
        "§3.10 const_expr 的 op_form 不得带 .ieee；mul.ieee 形态错误 -> "
        "CONST_FORM；§12.5.1 后缀 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const F: float32 = mul.@HERE@ieee float32, 2.0f, 3.0f\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0012",
        "§3.10 正向允许集不含 copy；const 初始化器 copy 标量 -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const X: int32 = @HERE@copy int32, 1\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0013",
        "§3.10 正向允许集不含 store；@unsafe 函数内的局部 const 使用 store "
        "-> CONST_FORM（首个非法子表达式操作码 Token）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public @unsafe func f(p: ptr<int32>) void then\n"
         "    const X: int32 = @HERE@store int32, p, 1\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0014",
        "§3.10 正向允许集不含 trap；#program const 初始化器使用 trap -> "
        "CONST_FORM（操作码 Token）",
        {"root.tc": PROGRAM +
         "const X: int32 = @HERE@trap\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0015",
        "§2.6/§3.10/§12.3 @padding(N) 的 N 必须是非负十进制字面量，进制前缀"
        "不合法 -> CONST_FORM（SEM，S3 已覆盖同一规则，S5 只作回归锚点）；"
        "§12.5.1 定位到 N",
        {"root.tc": PROGRAM +
         "struct S then\n"
         "    var x: int32 @padding(@HERE@0x10)\n"
         "end\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    _case(
        "TC-CT-FORM-0016",
        "§3.10 正向允许集不含 I/O 出向指令：const 初始化器中的 write 不在 "
        "const_expr 形态内 -> CONST_FORM（SEM）；§12.5.1 定位到违规 op",
        {"root.tc": PROGRAM +
         "const K: int32 = @HERE@write int64, 5\n"},
        "SEM", "TC_CE_CONST_FORM",
    ),
    # ------------------------------------------------------------------
    # CT evaluation failures (§3.10/§4.10/§6.3/§6.6.1).  Primary position is
    # the target declaration; the convention is the declaration identifier.
    _case(
        "TC-CT-OVERFLOW-0001",
        "§3.10/§6.3 有符号 strict 整数加法溢出 -> CONST_OVERFLOW；"
        "§4.10 失败归属目标声明；§12.5.1 主位置为常量声明，约定取声明标识符 "
        "BAD 首 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
    ),
    _case(
        "TC-CT-OVERFLOW-0002",
        "§3.10/§6.3 strict 浮点乘法有限结果舍入为 inf -> CONST_OVERFLOW；"
        "§12.5.1 目标声明（声明标识符约定）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float64 = mul float64, 1e308, 10.0\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
    ),
    _case(
        "TC-CT-OVERFLOW-0003",
        "§3.10/§6.3 strict shl：非零 a 且 k>=n（1<<40 in int32）不可表示 -> "
        "CONST_OVERFLOW；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = shl int32, 1, 40\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
    ),
    _case(
        "TC-CT-OVERFLOW-0004",
        "§3.10/§6.3/§13.2.2 isize 的 INT_MAX 按 W 实例化；w32 下 "
        "INT_MAX+1 strict 溢出 -> CONST_OVERFLOW；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: isize = add isize, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
        profile="w32",
    ),
    _case(
        "TC-CT-DIVZERO-0001",
        "§3.13.2/§3.10 const 初始化器中整数除数为常量 0 -> CONST_DIV_ZERO；"
        "§12.5.1 目标声明（声明标识符约定）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = div int32, 1, 0\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-DIVZERO-0002",
        "§3.10/§6.3.1 strict 浮点除零（有限非零 / ±0）-> CONST_DIV_ZERO；"
        "§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float32 = div float32, 1.0f, 0.0f\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-DIVZERO-0003",
        "§3.10/§6.3.3 strict 浮点 mod 的零除数（有限非零 a, b=0）-> "
        "CONST_DIV_ZERO；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float32 = mod float32, 1.0f, 0.0f\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-SHIFT-0001",
        "§3.10/§6.3 移位计数为负 -> CONST_SHIFT（shl）；§12.5.1 目标声明"
        "（声明标识符约定）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = shl int32, 1, -1\n"},
        "CT", "TC_CE_CONST_SHIFT",
    ),
    _case(
        "TC-CT-SHIFT-0002",
        "§3.10/§6.3 移位计数为负 -> CONST_SHIFT（shr）；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = shr int32, -8, -1\n"},
        "CT", "TC_CE_CONST_SHIFT",
    ),
    _case(
        "TC-CT-FLOATINVALID-0001",
        "§3.10/§6.3.1 strict 浮点 NaN 输入为无效操作 -> CONST_FLOAT_INVALID；"
        "§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float32 = add float32, nan, 1.0f\n"},
        "CT", "TC_CE_CONST_FLOAT_INVALID",
    ),
    _case(
        "TC-CT-FLOATINVALID-0002",
        "§3.10/§6.3.1 strict 浮点 0×inf 为无效操作 -> CONST_FLOAT_INVALID；"
        "§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float64 = mul float64, 0.0, inf\n"},
        "CT", "TC_CE_CONST_FLOAT_INVALID",
    ),
    _case(
        "TC-CT-FLOATINVALID-0003",
        "§3.10/§6.3.1 strict 浮点 0/0 属无效操作行而非除零行 -> "
        "CONST_FLOAT_INVALID，不报 CONST_DIV_ZERO；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float64 = div float64, 0.0, 0.0\n"},
        "CT", "TC_CE_CONST_FLOAT_INVALID",
    ),
    _case(
        "TC-CT-FLOATUNDERFLOW-0001",
        "§3.10/§6.3.2 strict 浮点下溢（tininess-after-rounding，结果非精确）"
        "-> CONST_FLOAT_UNDERFLOW；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float32 = mul float32, 1e-30f, 1e-10f\n"},
        "CT", "TC_CE_CONST_FLOAT_UNDERFLOW",
    ),
    _case(
        "TC-CT-FLOATUNDERFLOW-0002",
        "§3.10/§6.3.2 float64 1e-400 非精确下溢到 0 -> "
        "CONST_FLOAT_UNDERFLOW；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float64 = mul float64, 1e-200, 1e-200\n"},
        "CT", "TC_CE_CONST_FLOAT_UNDERFLOW",
    ),
    _case(
        "TC-CT-CAST-0001",
        "§3.10/§6.6.1 整数 cast 越界（2^31 -> int32）-> CONST_CAST；"
        "源字面量按 cast 上下文为 int64 且可表示；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = cast int32, 2147483648\n"},
        "CT", "TC_CE_CONST_CAST",
    ),
    _case(
        "TC-CT-CAST-0002",
        "§3.10/§6.6.1 浮点->整数 cast 越界（1e10 -> int32）-> CONST_CAST；"
        "§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: int32 = cast int32, 1e10\n"},
        "CT", "TC_CE_CONST_CAST",
    ),
    _case(
        "TC-CT-CAST-0003",
        "§3.10/§6.6.1 float64->float32 有限值转为 inf -> CONST_CAST；"
        "§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: float32 = cast float32, 1e308\n"},
        "CT", "TC_CE_CONST_CAST",
    ),
    _case(
        "TC-CT-CAST-0004",
        "§3.10/§6.6.1/§13.2.2 isize 目标范围按 W；w32 下 2^32 不可表示 "
        "-> CONST_CAST，w64 同一源码接受；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@BAD: isize = cast isize, 4294967296\n"},
        "CT", "TC_CE_CONST_CAST",
        profile="w32",
    ),
    _case(
        "TC-CT-CAST-0005",
        "§3.10/§6.6.1/§6.13/§13.2.2 sizeof isize 为 W，故移位指数 "
        "sizeof(isize)*8-2 = 30(w32)/62(w64)，X = 2^30/2^62；w64 下 "
        "cast int32 越界 -> CONST_CAST，w32 同一源码接受（W 反向翻转）；"
        "§12.5.1 目标声明（声明标识符 Y）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: usize = sizeof isize\n"
         "public const B: usize = mul usize, Self.S, 8\n"
         "public const E: usize = sub usize, Self.B, 2\n"
         "public const X: usize = shl usize, 1, Self.E\n"
         "public const @HERE@Y: int32 = cast int32, Self.X\n"},
        "CT", "TC_CE_CONST_CAST",
        profile="w64",
    ),
    # ------------------------------------------------------------------
    # CT start order, caching-visible diagnostics and failure attribution
    # (§4.7/§4.10).  The diamond "evaluated once" property is not observable
    # through diagnostics; these cases pin the observable parts instead.
    _case(
        "TC-CT-ORDER-0001",
        "§4.7/§5.2 同模块只能引用更早的 const；前向引用 Self.B -> "
        "UNDEFINED_NAME（SEM，不是 CT）；§12.5.1 未解析标识符（Self. 后的 B）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = Self.@HERE@B\n"
         "public const B: int32 = 1\n"},
        "SEM", "TC_CE_UNDEFINED_NAME",
    ),
    _case(
        "TC-CT-ORDER-0002",
        "§4.7/§5.2 const 自引用 -> UNDEFINED_NAME（SEM）；§12.5.1 "
        "未解析标识符（Self. 后的 A）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = Self.@HERE@A\n"},
        "SEM", "TC_CE_UNDEFINED_NAME",
    ),
    _case(
        "TC-CT-ORDER-0003",
        "§4.10 CT 按模块内源序启动；同模块两个独立失败 A(div0)、B(overflow)"
        "时只报源序最早的 A -> CONST_DIV_ZERO；B 不再求值；§12.5.1 目标声明",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const @HERE@A: int32 = div int32, 1, 0\n"
         "public const B: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-ORDER-0004",
        "§4.10 CT 启动顺序 module_order=root,a,b；a、b 各有一个独立失败，"
        "报遍历中最早的 a.A -> CONST_DIV_ZERO（b.B 不报）；§12.5.1 目标声明",
        {"root.tc": PROGRAM + "import a\nimport b\n",
         "a.tc": LIB +
         "public const @HERE@A: int32 = div int32, 1, 0\n",
         "b.tc": LIB +
         "public const B: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-ORDER-0005",
        "§4.10 改变 import 源序后 module_order=root,b,a，首个失败随之为 "
        "b.B -> CONST_OVERFLOW（a.A 不报）；§12.5.1 目标声明",
        {"root.tc": PROGRAM + "import b\nimport a\n",
         "a.tc": LIB +
         "public const A: int32 = div int32, 1, 0\n",
         "b.tc": LIB +
         "public const @HERE@B: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
    ),
    _case(
        "TC-CT-ORDER-0006",
        "§4.10 初始化器内常量引用从左到右递归求值：user.R 先引用 b.B1 再引用 "
        "a.A2，b.B1 失败即停止；失败归属 b 的声明（主位置在 b.tc，user 无级联），"
        "a.A2 与 user.LATER 不报；§12.5.1 目标声明",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import a\n"
         "import b\n"
         "public const R: int32 = add int32, b.B1, a.A2\n"
         "public const LATER: int32 = mul int32, 2147483647, 2\n",
         "a.tc": LIB +
         "public const A2: int32 = add int32, 2147483647, 1\n",
         "b.tc": LIB +
         "public const @HERE@B1: int32 = div int32, 1, 0\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-ORDER-0007",
        "§4.10 make 的命名字段按书写顺序求值：user.SM 的 x 先引用 a.B1，y 后"
        "引用 b.B2；a.B1 除零先失败并归属 a 的声明（user 无级联，b.B2 不报）；"
        "§12.5.1 目标声明",
        {"root.tc": PROGRAM + "import user\n",
         "user.tc": LIB +
         "import a\n"
         "import b\n"
         "public struct S then\n"
         "    var x: int32\n"
         "    var y: int32\n"
         "end\n"
         "public const SM: S = make S, x: a.B1, y: b.B2\n",
         "a.tc": LIB +
         "public const @HERE@B1: int32 = div int32, 1, 0\n",
         "b.tc": LIB +
         "public const B2: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    # ------------------------------------------------------------------
    # static var initializer failure and the SEM-before-CT boundary.
    _case(
        "TC-CT-STATIC-0001",
        "§4.7/§4.4 static var 初始化器必须是 const_expr，失败是编译期错误；"
        "strict 溢出 -> CONST_OVERFLOW，归属 static var 目标声明；§12.5.1 "
        "目标声明（声明标识符约定）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public static var @HERE@BAD: int32 = add int32, 2147483647, 1\n"},
        "CT", "TC_CE_CONST_OVERFLOW",
    ),
    _case(
        "TC-CT-LOCAL-0001",
        "§5.2/§4.10 函数内局部 const 也要编译期求值；strict 整数除零 -> "
        "CT CONST_DIV_ZERO，归属于该局部 const 声明；模块级 const 先于局部 "
        "const 求值；§12.5.1 目标声明（声明标识符约定）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public func f() int32 then\n"
         "    const @HERE@BAD: int32 = div int32, 1, 0\n"
         "    var r: int32 = copy int32, BAD\n"
         "    _ = return r\n"
         "end\n"},
        "CT", "TC_CE_CONST_DIV_ZERO",
    ),
    _case(
        "TC-CT-STAGE-0001",
        "§12.1/§12.2 阶段优先：源中同时有 SEM 类型错误和 CT 除零时先报 SEM；"
        "const 的除零在 SEM 只是待 CT 的错误；§12.5.1 首个不匹配操作数",
        {"root.tc": PROGRAM +
         "const BAD: int32 = div int32, 1, 0\n"
         "var b: bool = copy bool, true\n"
         "var x: int32 = copy int32, @HERE@b\n"},
        "SEM", "TC_CE_TYPE_MISMATCH",
    ),
    # ------------------------------------------------------------------
    # W=32/W=64 differences that reach SEM/CT (§13.2.2).
    _case(
        "TC-CT-LITERAL-0001",
        "§6.6.3/§13.2.2 usize 字面量范围按 W；w32 下 2^32 不可表示 -> "
        "LITERAL_RANGE（SEM），w64 同一源码接受；§12.5.1 字面量首 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const U: usize = @HERE@4294967296\n"},
        "SEM", "TC_CE_LITERAL_RANGE",
        profile="w32",
    ),
    _case(
        "TC-CT-LITERAL-0002",
        "§6.6.3/§13.2.2 isize 字面量范围按 W；w32 下 2^31 不可表示 -> "
        "LITERAL_RANGE（SEM），w64 同一源码接受；§12.5.1 字面量首 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const I: isize = @HERE@2147483648\n"},
        "SEM", "TC_CE_LITERAL_RANGE",
        profile="w32",
    ),
    _case(
        "TC-CT-WIDTH-0001",
        "§3.11/§13.2.2 trunc 目标位宽 >= 源位宽：w64 下 trunc isize,int64 "
        "的 isize 为 64 位 -> TRUNC_WIDTH（SEM），w32 下 32<64 合法；"
        "§12.5.1 目标类型 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const X: isize = trunc @HERE@isize, 4294967296\n"},
        "SEM", "TC_CE_TRUNC_WIDTH",
        profile="w64",
    ),
    _case(
        "TC-CT-WIDTH-0002",
        "§3.11/§13.2.2 trunc int32,isize：w32 下源 isize 为 32 位，目标位宽 "
        "不小于源 -> TRUNC_WIDTH（SEM），w64 下 32<64 合法；§12.5.1 目标类型 Token",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: isize = 1\n"
         "public const X: int32 = trunc @HERE@int32, Self.S\n"},
        "SEM", "TC_CE_TRUNC_WIDTH",
        profile="w32",
    ),
    _case(
        "TC-CT-WIDTH-0003",
        "§3.11/§13.2.2 bitcast isize<->int32 仅 w32 等宽；w64 下类别允许但"
        "位宽不等 -> BITCAST_WIDTH（SEM），w32 接受；§12.5.1 源操作数首 Token"
        "（源操作数是限定名 Self.S 时，约定取 Self 的首 Token）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: isize = 1\n"
         "public const X: int32 = bitcast int32, @HERE@Self.S\n"},
        "SEM", "TC_CE_BITCAST_WIDTH",
        profile="w64",
    ),
]


POSITIVE = [
    _positive(
        "TC-TOOL-CT-LIT-0001",
        "§3.10/§6.6.3 常量字面量四类：整数/浮点（含 f 后缀）/bool/nullptr 作为 ptr 值；"
        "全部是 const_expr 允许的源",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const I: int32 = -7\n"
         "public const F64: float64 = 1.5\n"
         "public const F32: float32 = 2.5f\n"
         "public const B: bool = false\n"
         "public const P: ptr<int32> = nullptr\n"},
    ),
    _positive(
        "TC-TOOL-CT-REF-0001",
        "§3.10 此前定义的 const 引用与 Self.K：Self.A、Self.B 及纯运算组合",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = 1\n"
         "public const B: int32 = Self.A\n"
         "public const C: int32 = add int32, Self.A, Self.B\n"},
    ),
    _positive(
        "TC-TOOL-CT-XMOD-0001",
        "§3.10/§4.10 跨模块 public const 引用 m.K；导入模块内 Self.K 链式",
        {"root.tc": PROGRAM +
         "import lib\n"
         "const R: int32 = lib.K\n"
         "var x: int32 = copy int32, R\n",
         "lib.tc": LIB +
         "public const K: int32 = 7\n"
         "public const K2: int32 = add int32, Self.K, 1\n"},
    ),
    _positive(
        "TC-TOOL-CT-CHAIN-0001",
        "§3.10 const 结构体字段逐层读取（Self.O.a.z，每层中间结果都是 const "
        "结构体）与 make 命名字段",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public struct Inner then\n"
         "    var z: int32\n"
         "end\n"
         "public struct Outer then\n"
         "    var a: Inner\n"
         "end\n"
         "public const I: Inner = make Inner, z: 7\n"
         "public const O: Outer = make Outer, a: Self.I\n"
         "public const Z: int32 = Self.O.a.z\n"},
    ),
    _positive(
        "TC-TOOL-CT-OPS-0001",
        "§3.10/§6.3/§6.4/§6.5 纯运算全组：add/sub/mul/div/mod/neg/abs/and/or/xor/not/"
        "shl/shr/eq/ne/lt/le/gt/ge；整数与 bool 两种类型族，结果均可表示",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const ADD: int32 = add int32, 7, 3\n"
         "public const SUB: int32 = sub int32, 7, 3\n"
         "public const MUL: int32 = mul int32, 6, 7\n"
         "public const DIV: int32 = div int32, 7, 2\n"
         "public const MOD: int32 = mod int32, 7, 2\n"
         "public const NEG: int32 = neg int32, 5\n"
         "public const ABS: int32 = abs int32, -5\n"
         "public const AND: int32 = and int32, 6, 3\n"
         "public const OR: int32 = or int32, 6, 3\n"
         "public const XOR: int32 = xor int32, 6, 3\n"
         "public const NOT: int32 = not int32, 0\n"
         "public const SHL: int32 = shl int32, 3, 2\n"
         "public const SHR: int32 = shr int32, -8, 1\n"
         "public const EQ: bool = eq int32, 1, 1\n"
         "public const NE: bool = ne int32, 1, 2\n"
         "public const LT: bool = lt int32, 1, 2\n"
         "public const LE: bool = le int32, 1, 1\n"
         "public const GT: bool = gt int32, 2, 1\n"
         "public const GE: bool = ge int32, 2, 2\n"
         "public const BAND: bool = and bool, true, false\n"
         "public const BOR: bool = or bool, true, false\n"
         "public const BXOR: bool = xor bool, true, false\n"
         "public const BNOT: bool = not bool, false\n"},
    ),
    _positive(
        "TC-TOOL-CT-CONV-0001",
        "§3.10/§6.6 cast/trunc/数值 bitcast：同型与跨型 cast、窄化 trunc、"
        "整数↔浮点与整数↔整数等宽 bitcast 均为允许源",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int64 = cast int64, 7\n"
         "public const B: float32 = cast float32, 3.5\n"
         "public const C: int16 = trunc int16, 70000\n"
         "public const D: uint32 = bitcast uint32, 8\n"
         "public const E: float64 = bitcast float64, 4607182418800017408\n"
         "public const F: int32 = cast int32, Self.A\n"
         "public const G: int16 = trunc int16, Self.A\n"},
    ),
    _positive(
        "TC-TOOL-CT-MAKE-0001",
        "§3.10/§6.2.7 make 位置字段与命名字段两种形式（命名字段保持声明顺序）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public struct P then\n"
         "    var x: int32\n"
         "    var y: bool\n"
         "end\n"
         "public const A: P = make P, 1, true\n"
         "public const B: P = make P, x: 2, y: false\n"},
    ),
    _positive(
        "TC-TOOL-CT-SIZEOF-0001",
        "§3.10/§6.13 sizeof 作为 const_expr：标量/指针/函数指针/结构体大小",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public struct S then\n"
         "    var a: int8\n"
         "    var b: int32\n"
         "end\n"
         "public const N: usize = sizeof int32\n"
         "public const P: usize = sizeof ptr<int32>\n"
         "public const F: usize = sizeof fnptr<int32>\n"
         "public const Z: usize = sizeof S\n"},
    ),
    _positive(
        "TC-TOOL-CT-STATIC-0001",
        "§3.10/§4.7 static var 用 const 标量与 const 结构体初始化；"
        "成功值与 const 使用处内联无关",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public struct S then\n"
         "    var x: int32\n"
         "end\n"
         "public const K: int32 = 7\n"
         "public const CS: S = make S, x: Self.K\n"
         "public static var V: int32 = Self.K\n"
         "public static var W: S = Self.CS\n"},
    ),
    # ------------------------------------------------------------------
    # One distinct "no false positive" acceptor near each CT trigger.
    _positive(
        "TC-TOOL-CT-NOFP-0001",
        "§6.3 strict 整数边界不溢出：INT_MAX-1+1、INT_MIN+0、neg/abs(INT_MIN+1) "
        "均合法（与 TC-CT-OVERFLOW-0001 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = add int32, 2147483646, 1\n"
         "public const B: int32 = add int32, -2147483648, 0\n"
         "public const C: int32 = neg int32, -2147483647\n"
         "public const D: int32 = abs int32, -2147483647\n"},
    ),
    _positive(
        "TC-TOOL-CT-NOFP-0002",
        "§6.3 非零除数：整数 div/mod 与 strict 浮点 div/mod 均不报 "
        "CONST_DIV_ZERO（与 TC-CT-DIVZERO-0002/0003 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = div int32, 1, 2\n"
         "public const B: int32 = mod int32, -7, 2\n"
         "public const C: float32 = div float32, 1.0f, 2.0f\n"
         "public const D: float64 = mod float64, 5.0, 2.0\n"},
    ),
    _positive(
        "TC-TOOL-CT-NOFP-0003",
        "§6.3 非负计数：a=0 的 k>=n strict shl 结果 0；shr 大计数为 0；"
        "shr 负数算术右移；shl 结果可表示（与 TC-CT-SHIFT-0001 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = shl int32, 0, 40\n"
         "public const B: int32 = shr int32, 123, 40\n"
         "public const C: int32 = shr int32, -8, 1\n"
         "public const D: int32 = shl int32, 1, 30\n"},
    ),
    _positive(
        "TC-TOOL-CT-NOFP-0004",
        "§6.3.1 有限非 NaN 输入、非 0×inf、非零除数：strict 浮点普通运算合法"
        "（与 TC-CT-FLOATINVALID-0002 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: float32 = add float32, 1.0f, 2.0f\n"
         "public const B: float64 = mul float64, 0.0, 2.0\n"
         "public const C: float32 = div float32, 1.0f, 2.0f\n"
         "public const D: float32 = mod float32, 5.0f, 2.0f\n"},
    ),
    _positive(
        "TC-TOOL-CT-NOFP-0005",
        "§6.3.2 精确非规格化不触发下溢：min-normal×0.5 精确为非规格化；"
        "×0 精确为 0；min-subnormal×2 精确回 min-normal（与 "
        "TC-CT-FLOATUNDERFLOW-0001 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: float32 = mul float32, 1.1754944e-38f, 0.5f\n"
         "public const B: float64 = mul float64, 1e-300, 0.0\n"
         "public const C: float32 = mul float32, 5.877472e-39f, 2.0f\n"},
    ),
    _positive(
        "TC-TOOL-CT-NOFP-0006",
        "§6.6.1 cast 目标范围内：整数边界、uint32 上界、float64->float32 "
        "有限、int64 表示 2^32 均合法（与 TC-CT-CAST-0001/0003 相邻）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: int32 = cast int32, 2147483647\n"
         "public const B: int32 = cast int32, -2147483648\n"
         "public const C: uint32 = cast uint32, 4294967295\n"
         "public const D: float32 = cast float32, 1e30\n"
         "public const E: int64 = cast int64, 4294967296\n"},
    ),
    # ------------------------------------------------------------------
    # W-sensitive positives and the opposite-direction W width flips.
    _positive(
        "TC-TOOL-CT-WCAST-0001",
        "§13.2.2 w64 isize 的 INT_MAX+1 与 2^32 cast 均可表示；与 w32 的 "
        "TC-CT-OVERFLOW-0004/TC-CT-CAST-0004 成对（w32 失败、w64 接受）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: isize = add isize, 2147483647, 1\n"
         "public const B: isize = cast isize, 4294967296\n"},
        profile="w64",
    ),
    _positive(
        "TC-TOOL-CT-WCASTVICE-0001",
        "§6.13/§13.2.2 w32 下 sizeof isize=4，X=2^30 可 cast 到 int32；"
        "与 w64 的 TC-CT-CAST-0005 成对（w64 失败、w32 接受，CT 结果随 W 反向翻转）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: usize = sizeof isize\n"
         "public const B: usize = mul usize, Self.S, 8\n"
         "public const E: usize = sub usize, Self.B, 2\n"
         "public const X: usize = shl usize, 1, Self.E\n"
         "public const Y: int32 = cast int32, Self.X\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-CT-WLIT-0001",
        "§13.2.2 w64 isize/usize 字面量范围覆盖 2^31/2^32；与 w32 的 "
        "TC-CT-LITERAL-0001/0002 成对（w32 失败、w64 接受）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const U: usize = 4294967296\n"
         "public const I: isize = 2147483648\n"},
        profile="w64",
    ),
    _positive(
        "TC-TOOL-CT-WTRUNC-0001",
        "§13.2.2 w32 trunc isize,int64 目标 32 < 源 64 合法；与 w64 的 "
        "TC-CT-WIDTH-0001 成对（w64 失败、w32 接受）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const X: isize = trunc isize, 4294967296\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-CT-WTRUNC-0002",
        "§13.2.2 w64 trunc int32,isize 目标 32 < 源 64 合法；与 w32 的 "
        "TC-CT-WIDTH-0002 成对（w32 失败、w64 接受）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: isize = 1\n"
         "public const X: int32 = trunc int32, Self.S\n"},
        profile="w64",
    ),
    _positive(
        "TC-TOOL-CT-WBITCAST-0001",
        "§13.2.2 w32 bitcast int32,isize 等宽合法；与 w64 的 "
        "TC-CT-WIDTH-0003 成对（w64 失败、w32 接受）",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const S: isize = 1\n"
         "public const X: int32 = bitcast int32, Self.S\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-CT-SIZEOF-0002",
        "§3.10/§6.13/§13.2.1 w32 下 sizeof isize/usize/ptr/fnptr 为 4；"
        "结果 const、不作为诊断可观察值，接受即证明不误报",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: usize = sizeof isize\n"
         "public const B: usize = sizeof usize\n"
         "public const C: usize = sizeof ptr<int32>\n"
         "public const D: usize = sizeof fnptr<int32>\n"},
        profile="w32",
    ),
    _positive(
        "TC-TOOL-CT-SIZEOF-0003",
        "§3.10/§6.13/§13.2.1 w64 下 sizeof isize/usize/ptr/fnptr 为 8；"
        "结果 const、不作为诊断可观察值，接受即证明不误报",
        {"root.tc": ROOT_IMPORT_LIB,
         "lib.tc": LIB +
         "public const A: usize = sizeof isize\n"
         "public const B: usize = sizeof usize\n"
         "public const C: usize = sizeof ptr<int32>\n"
         "public const D: usize = sizeof fnptr<int32>\n"},
        profile="w64",
    ),
]


def _write_compile_case(case, failures, check):
    case_dir = HERE / "ct"
    fixture = case_dir / case["id"].lower()
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
    source = f"{case['id'].lower()}/{case['source']}"
    primary = (
        f"{case['id'].lower()}/{primary_file}",
        primary_pos[0],
        primary_pos[1],
    )
    related = None
    if related_file is not None:
        related = (
            f"{case['id'].lower()}/{related_file}",
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
    case_dir = HERE / "accept_s5"
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
    argv = ["tc-vm", "--static-only"]
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
        fixture = HERE / "ct" / case["id"].lower()
        for name in case["files"]:
            compile_paths.add(fixture / name)
        compile_paths.add(HERE / "ct" / f"{case['id'].lower()}.yaml")
    for case in POSITIVE:
        fixture = HERE / "accept_s5" / case["id"].lower()
        for name in case["files"]:
            positive_paths.add(fixture / name)
        positive_paths.add(HERE / "accept_s5" / f"{case['id'].lower()}.yaml")
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
    seen = set()
    for case in CASES + POSITIVE:
        if case["id"] in seen:
            failures.append(f"{case['id']}: duplicate case id")
        seen.add(case["id"])
    for case in CASES:
        _write_compile_case(case, failures, args.check)
    for case in POSITIVE:
        _write_positive_case(case, failures, args.check)
    expected_compile, expected_positive = _expected_artifacts()
    for label, expected, root in (
        ("ct", expected_compile, HERE / "ct"),
        ("accept_s5", expected_positive, HERE / "accept_s5"),
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
        f"s5 case table: {len(CASES)} compile, {len(POSITIVE)} tool cases"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

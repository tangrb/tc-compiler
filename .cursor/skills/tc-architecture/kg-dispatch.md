# 分发细节 — 浮点 / cast / bitcast / 复合 RHS

由 `@knowledge-graph` 索引指向；勿与其它 `kg-*.md` 同时整读。8 分发点表见索引 rule。

> **34 RHS 全可解析**（`tc_types.h`）。Executor/AOT 全覆盖；`tc_eval_const_rhs` 对 ptr/memblock/field/funcall/self **defer**（let 禁）。覆盖闸门：`check_rhs_coverage.py`（per-point skip，无全局 Phase1 reserved）。

## 标量 16 RHS

`LIT` `CONST_REF` `ARITH` `UNARY` `COMPARE` `LOGIC_BIN` `LOGIC_UN` `BITWISE_BIN` `BITWISE_UN` `SHIFT` `CAST` `CONST_CAST` `FLOAT_ARITH` `FLOAT_UNARY` `FLOAT_COMPARE` `BITCAST`

## 复合 / 调用 18 RHS（已落地）

`MEMBLOCK_LOAD` `MEMBLOCK_CONSTRUCTOR` `MEMBLOCK_COUNT` `STRUCT_CONSTRUCTOR` `FIELD_READ` `PTR_LOAD` `PTR_ADDRESS` `PTR_ADD` `PTR_SUB` `PTR_EQ` `PTR_NE` `PTR_LT` `PTR_LE` `PTR_GT` `PTR_GE` `PTR_SIZE` `FUNCALL_EXPR` `SELF_MEMBER`

| 层 | 覆盖 | 备注 |
|----|------|------|
| Parser | 34/34 | `FUNCALL_EXPR` 在 `tc_parser_stmt.c` 赋 kind；其余多在 `tc_parser_rhs.c` |
| Free / Pass2 | 34/34 | 复合 Pass2 经 `tc_type_check_rhs` → `tc_*_check.c` |
| const_eval | 标量 + bitcast + **struct ctor** | 其余复合/funcall **skip** |
| Executor / AOT | 34/34 | shim：`tc_aot_{ptr,memblock,struct}_*`；funcall 内联发射 |

## 浮点 / cast / bitcast

```
tc_parse_type_token: float32/float64 → FloatType
tc_parse_arith_or_compare_rhs(type):
  整数 → ARITH/UNARY/COMPARE；浮点 → FLOAT_ARITH/UNARY/COMPARE
tc_parse_mode_keyword(type):
  整数 → wrap（TcWrapMode）；浮点算术 → ieee；strict 默认；浮点 wrap 只作非法哨兵
tc_parse_cast_rhs: 数值 cast/truncate → TC_RHS_CAST；等宽位复制 → TC_RHS_BITCAST
tc_eval_rhs / tc_eval_const_rhs / AOT shim → tc_sem_cast.c 的 tc_exec_cast/truncate/bitcast
```

隔离：浮点禁 mod/位运算/移位/逻辑短路/wrap；整数禁 ieee；浮点比较和一元操作禁模式；truncate 仅整数窄化；bitcast 禁 bool 且须等宽。

let：`tc_eval_const_rhs` 每步按声明精度舍入，允许与 runtime 相同的整数 wrap、浮点 ieee、整数 truncate 与 bitcast；禁止嵌套调用与 `FUNCALL_EXPR`。

AOT shim：`tc_aot_fp_arith/unary/compare` + 共享 `tc_aot_cast/truncate/bitcast`；`tc_aot_lit` 浮点类型直接返回 IEEE bits；`tc_aot_format_enum` 映射 5 浮点格式符。

分层改文件：[features.md](features.md) 对应 § · 测试：[test-map.md](test-map.md)

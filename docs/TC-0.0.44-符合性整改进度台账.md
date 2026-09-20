# TC 0.0.44 符合性整改进度台账

> **性质**：过程文档（进度记录）。**只有本文件承载「已完成/待办」状态**；[TC 0.0.44 语言标准符合性审计报告](./TC-0.0.44-语言标准符合性审计报告.md) 保持为纯清单，不回填修复状态。
> **权威基准**：[TC 语言标准设计说明书 0.0.44](./TC语言标准设计说明书-0.0.44.md)（不改）。
> **执行纪律**：严格串行（不开子代理、不并发）；每完成一份文档或一条修复即单独提交（**不推送**）。

## 状态图例

| 标记 | 含义 |
| ---- | ---- |
| ☐ | 待办 |
| ◐ | 进行中 |
| ☑ | 已完成（含门禁通过） |
| ⊘ | 跳过 —— 需标准 owner 裁决，本轮不动 |

---

## 阶段 1：设计文档与标准对齐（依据审计报告 §3 的 C 系列）

| 顺序 | 文档 | 条目 | 状态 | 提交 |
| ---- | ---- | ---- | ---- | ---- |
| 1 | `TC编译器标准设计说明书-0.0.44.md` | C-1～C-9 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录 |
| 2 | `TC-VM详细设计说明书-0.0.44.md` | C-12～C-14 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录 |
| 3 | `TC-AOT详细设计说明书-0.0.44.md` | C-10～C-11 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录（无需改动） |
| 4 | `TC-Embed详细设计说明书-0.0.44.md` | C-20～C-21 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录 |
| 5 | `libtc设计说明书-0.0.44.md` | C-19 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录 |
| 6 | `TC-VM命令行参考-0.0.44.md` | C-15～C-18 ＋ §3.2 中属本文档的 P2 项 | ☑ | 见文末提交记录 |
| 7 | 跨文档一致性 | C-22、C-23、C-24 | ☑ | 见文末提交记录 |

### 阶段 1 逐条明细

**① 编译器标准**

| 条目 | 内容摘要 | 状态 |
| ---- | -------- | ---- |
| C-1 | §8.5 / §11.4.2 重定义 `FUNCALL_POSITION`/`FUNCALL_RESULT_TYPE`，与标准 §8.2.3＋附录 B.12 冲突 | ☑ |
| C-2 | §1.3 第 1 条以「13 阶段优先」覆盖标准 §11「同阶段按源序」；`let` 闭合缺 `var`/形参例外 | ☑ |
| C-3 | §4.6 把 `#program` 导入名冲突集合限缩为 `var`/`let` | ☑ |
| C-4 | §4.6 括注与第 3 条自相矛盾（同名模块多名称导入） | ☑ |
| C-5 | §3.2/§6.7/§11.4.6 把静态 `MEMCOPY_UNSAFE_INVALID_RANGE` 扩到负下标 | ☑ |
| C-6 | §11.4.1 `DUPLICATE_LABEL` 写「同一作用域」（应为「同一代码块」） | ☑ |
| C-7 | §3.6 `u`/`U` 字面量行内自相矛盾 | ☑ |
| C-8 | §6.0 模式矩阵把 `shr` 与 `div`/`mod`/`abs` 混为一谈 | ☑ |
| C-9 | §6.0/§3.5 无符号算术写 `wrap` 处「（语法阶段也可拒绝）」 | ☑ |

同批一并处理的本文档 §3.2 P2 项：§1.1.1 运算行补 `ptr_eq`/`ptr_ne`；§1.4 悬空引用 `§15.5`/`§6.1.2` 改指本文件 §6.7 与 [语言标准 §6.1.2]；§3.7 字面量源类型表补 `cast`×`bool` 与 `bitcast`×`inf`/`nan`；§4.3 `static let` 求值阶段统一为第 9 阶段（CT）、来源/形态检查留在 6d（SEM）；§5.1 函数签名收集改指第 4 阶段子阶段 4d；§8.1.1 `ptr<T>` 形参实参扩为任意 `operand`；§8.2 第 2/3/4 条补错误码与「数量超限」检查位次；§11.4.1 `BITCAST_WIDTH` 补「源字面量类型不匹配」。
（审计 P2 清单中「§1.4 引用 §9.1/既有未关闭项」「0.0.42 版本残留」经核对已不存在，无需改动。）

**② VM 详设**：C-12（§5.3 发明 `-> return_type` 且称可选）、C-13（§14.1 精度范围＋阶段归属）、C-14（§5.3 形参重复检查放 Parser）。

| 条目 | 核对结论 | 处置 |
| ---- | -------- | ---- |
| C-12 | §5.3 已改为「返回类型**必需**、无 `->` 后缀、不存在省略即 `void`」（L378-379） | 无需改动（已对齐） |
| C-13 | §14.1 已为 `width 1～65535`、`precision 0～65535`，且格式符归第 6e 阶段（SEM）、操作数个数归第 3 阶段（SYN）（L998-1000） | 无需改动（已对齐） |
| C-14 | §5.3 已写明「形参名重复不在语法阶段拦截，Parser 只收集形参名列表」，§15.4 亦列 `DuplicateParameter` 于第 5 阶段（SEM） | 仅修正 §5.3 的阶段笔误：第 6 阶段 → **第 5 阶段** |
| P2 | §1.5 同步表已无 D35 行/列错位；`§5.9` 死引用不存在；`§3.9.3`/`§3.5`/`§10.4` 均已带「[语言标准 …]」限定；`bitcast(ptr<T>, nullptr)` 已由「正例」改为「标准未明确定型」注记；阶段 13 已注明「执行不属于 13 阶段」 | 无需改动（已对齐） |
| 本轮新增发现 | §2.3 原文「`static var` 和 `static let` 在函数签名收集后、函数体分析前完成初始化器求值」与 §2.1 阶段 9 / §13.4 冲突 | 改为「`static let` 形态/来源在 6d（SEM）、求值在第 9 阶段（CT）；`static var` 形态验证在第 9 阶段、运行期求值在程序准备阶段」 |

**③ AOT 详设**：C-10（§11.1 指针 shim 为宿主地址模型）、C-11（§16.1 差分等价判据多 claim 且漏项）。

| 条目 | 核对结论 | 处置 |
| ---- | -------- | ---- |
| C-10 | §11.1 已给出槽号原型 —— `uint64_t tc_aot_ptr_address(int slot)`、`tc_aot_ptr_load(uint64_t *slots, uint64_t ptr_bits, …)`，并明文禁止 `&slots[X]` 等宿主地址（L668-701） | 无需改动（已对齐） |
| C-11 | §16.1 已列「四项、不多不少」可观察行为，并明确 stderr 文本/进程退出状态/宿主位模式**不是**可观察行为（L49、L859-866） | 无需改动（已对齐） |
| P2 | §4.3/§11.2 已区分区间拷贝 `tc_aot_memblock_copy`（9 参）与整块深拷贝 `tc_aot_memblock_clone`；§1.4 表格结构完好且已反映 9 项既有未关闭；死锚/`0.0.41`/`0.0.42` 残留不存在；§16.3 存在（无跳号）；§18 已重写为现役可移植性约定；`STRUCT_TOTAL_BYTES`/`NUM_MEMBLOCK_ALLOCS` 不存在；§15.1 已列 `--embed`；`确定初始化` 引用已改指 [语言标准 §9.2] | 无需改动（已对齐） |

**④ TC-Embed 详设**：C-20（§12/§15.8 的 4 个 TC 示例实为 C 语法）、C-21（§12.2/§15.8.3 把数据写进指针形参自身槽位）。

| 条目 | 处置 |
| ---- | ---- |
| C-20 | 4 段 `tc` 示例改写为合法 TC：`plus`（不占用保留字 `add`；`return` 只接 `operand`，先 `var r` 再返回）、`sum`（`while lt(...) then … end`、拆出 `ptr_add`/`ptr_load` 两步避免调用嵌套、索引与 `len` 用 `usize`）、`counter`（补 `public static var`、函数内用 `Self.count`）、`mylib`（`plus` + `scale`，`void` 显式 `return`）。**4 段均以 `tc-vm -c` 与 `tc-aot -c` 实测接受** |
| C-21 | §12.2/§15.8.3 改为 `tc_embed_tmp_begin` 分配**临时槽位区**平铺数据、`tc_embed_ptr_encode(base)` 传参、用毕 `tc_embed_tmp_end`，不再写 `param_slots[0]` 自身槽位；§5.4 同改为临时区；`len`/`n` 改 `usize`（`tc_value_from_uint64`） |
| P2 | §2.1/§1.4 槽位编码补「实现定义的抽象槽编码」定性；§3.2 文件表补 `tc_embed_internal.h`；§3.3 头骨架补 `tc_embed_slot_count`；§5.2 修正「只有一个调用帧活跃」表述；§10.2 消息格式改为宿主纯文本＋语言诊断格式；§10.3 静态错误归编译阶段（`tc_embed_create*` 不做静态检查）；§14.1 测试名改为实际 `test_embed_ptr_load_sum`/`test_embed_ptr_store_offset`；§15.1 `tc_pass1_collect_symbols` 片段与 `src` 对齐；§15.3.1 函数表改为内联数组（含哨兵），删除不存在的 `.inc`；§15.7.1 CMake 改为编入 `libtc`（无独立 target）；§15.11 `tc_embed_create_aot` 改为「仍需 `TcTypedProgram` 提取元数据」；§16.7 `sum` 示例补第二实参 |

**⑤ libtc 设计说明书**：C-19（§15.3 自相矛盾、§7.3 跨入口承诺与实现不符）。

| 条目 | 处置 |
| ---- | ---- |
| C-19 | §15.3 改写：明确内存入口**不执行 4b–4d**、含 `import` 的源不在其接受集内、只声明所覆盖阶段（[语言标准 §1.3] 部分阶段工具条款）；删除「执行 13 阶段（含 4a→4b→4c→4d）」的自相矛盾表述；§7.3（诊断契约）把「不同入口产生相同诊断」限定为「各自覆盖阶段范围内」；§3.1 图下与 §15.12 签名同步补覆盖差异 |
| P2 | §3.1 阶段 13 改为「VM / AOT 代码生成（执行不属 13 阶段）」；§7.3 诊断规则补全为五条（阶段优先/源位置/规则优先/关联位置/遍历无关）；§11.1 与 §15.9 bench 标签改为实测 `parse`/`analyze`/`analyze+modules`/`execute`；§11.3 明确分析器两个可变全局（`g_name_scope_members`/`g_name_scope_in_function`）→ **并发编译不受支持**；§15.4 删除「默认搜索路径」、同步并发限制；§14.1 自引改为 §7/§15.8 并补「非零未决」说明；§15.11 能力表标注内存入口覆盖差异；`合计实现枚举 87` 与 `86 语言码 + OutOfMemory` 经核对已正确 |

**⑥ VM 命令行参考**：C-15（§3.5 不存在的默认搜索路径）、C-16（§8.4 示例是非法 TC 程序）、C-17（§2.2 `-e` 示例与实际输出不符）、C-18（§6 完整映射表缺 `ExtraArgument`）。

| 条目 | 处置 |
| ---- | ---- |
| C-15 | §3.5 删除「默认搜索路径」，明确搜索顺序**穷尽**为「入口目录 → `-I`（按参数顺序）」，无内置目录/环境变量，未命中即 `ImportNotFound` |
| C-16 | §8.4 原示例对 `memblock` 取址（标准禁止）。改写为两段合法示例（标量指针 `ptr_address`/`ptr_load`/`ptr_store`/`ptr_size`/指针重标记；memblock 的 `memblock_load`/`store`/`.count`），并注明「先声明后执行」「操作数不得内嵌调用型 RHS」「不可对 memblock 取址」。**两段均以 `tc-vm` 实测通过** |
| C-17 | §2.2 与 §6.1 的 `-e` 示例由 `error [TC_CE_SYNTAX]` 改为实际的**打印名** `error [SyntaxError]`，并明确打印的是 `TcErrorKind` 名而非 `TC_CE_*` |
| C-18 | §6.5 增补 `ExtraArgument` 行；并核对该表覆盖实现全部打印名（86 个唯一名） |
| P2 | §2.1 用法改为 `tc-vm [options] <file.tc>`（文件必需）；`-I` 注明**最多 64 条**（`TC_MAX_INCLUDE_PATHS`）；§6.2 静态 `MemcopyUnsafeInvalidRange` 去掉「或下标」；`FunctionCallPositionError`/`FunctionCallResultTypeError` 条件按标准 §8.2.3 改写；§7.2 bench 标签改为 `parse`/`analyze+modules`/`execute`；§9.2 诊断规则补全为五条；§9.3 重复编号与「41+1→87」历史列经核对已不存在 |

**⑦ 跨文档**：C-22（三份文档同步状态表未反映未关闭项）、C-23（`FUNCALL_*` 定义重复出现在编译器标准与 CLI 参考）、C-24（`MEMCOPY_UNSAFE_INVALID_RANGE` 分工四种写法）。

| 条目 | 处置 |
| ---- | ---- |
| C-22 | AOT §1.4 原有「不代表实现侧零未决」段保留；本轮为 **VM §1.5** 补同口径段落（列出 9 项、点明字段/`memblock` 操作数元数据与顶层 `let` 下标两项 VM/AOT 分歧）；libtc §14.1/§15.11 已在上一步补注。三份同步状态表现口径一致：**「已同步」只表示该文档口径落地，不表示全仓零未决** |
| C-23 | `FUNCALL_POSITION`/`FUNCALL_RESULT_TYPE` 的触发条件已由 C-1 统一到标准 §8.2.3：编译器标准 §8.5/§11.4.2、CLI 参考 §6.5 三处一致（非 void 结果被丢弃 → POSITION；void 用作值 → RESULT_TYPE；非 void 接收类型不符 → `TypeMismatch`） |
| C-24 | `MEMCOPY_UNSAFE_INVALID_RANGE` 的分工已统一为附录 B/§11.1 读法：**静态只覆盖 `length < 0`，负下标归运行时 `TC_RE_*`**（编译器标准 §3.2/§6.7/§11.4.6；VM 详设 §12/§15.2；CLI 参考 §6.2/§6.5 全部一致）。标准自身 §6.8.9 的分裂记为标准待澄清项（不由实现侧文档裁决） |

---

## 阶段 2：实现逐条修复（依据审计报告 §2）

**顺序**：B-1 → B-2 → … → B-56 → B-57 → … → B-66，以及 §2.6 的 15 条子项。每条闭环＝改 `src/` → 补/改用例 → 最小测试与相关门禁通过 → 提交。

### B-1～B-56

| 条目 | 主题 | 状态 | 提交 |
| ---- | ---- | ---- | ---- |
| B-1 | `funcall` 位置码/结果码互换 | ☑ | 见文末提交记录 |
| B-2 | SEM 首个诊断定序（源序） | ☑ | 见文末提交记录 |
| B-3 | 重复格式标志错报 `SYNTAX` | ☑ | 见文末提交记录 |
| B-4 | `let` RHS 为 `var`/形参时错报 | ☑ | 见文末提交记录 |
| B-5 | `memblock` `N`/`count:` 来源校验过宽＋错码 | ☑ | 见文末提交记录 |
| B-6 | 实参个数检查遮蔽重复/未知/顺序 | ☑ | 见文末提交记录 |
| B-7 | VM 按槽位标签渲染 `write` | ☑ | 见文末提交记录 |
| B-8 | 结构体字段指针操作数：VM 内部错误 / AOT 空指针 | ☑ | 见文末提交记录 |
| B-9 | 顶层 `let` 作 `memblock_copy` 下标：仅 VM 失败 | ☑ | 见文末提交记录 |
| B-10 | `bitcast(ptr<T>, usize)` 解引用两后端分歧 | ☑ | 见文末提交记录 |
| B-11 | 指针别名清零后读字段：VM 内部错误 | ☑ | 见文末提交记录 |
| B-12 | `#program` 结构体名 vs `import` 名冲突被放过 | ☐ | |
| B-13 | libtc 内存入口不解析 `import` | ☐ | |
| B-14 | 依赖模块诊断定位到入口文件 | ☐ | |
| B-15 | `tc-aot -r` 相对路径失败 | ☐ | |
| B-16 | 模块文件 I/O 失败映射为语言码 | ☐ | |
| B-17 | `#lib static var` 缺初始化器错码 | ☐ | |
| B-18 | `implementation error` 泄漏 | ☑ | 见文末提交记录 |
| B-19 | embed 临时槽区与声明槽区重叠 | ☐ | |
| B-20 | `memcopy_unsafe` 操作数类型不校验 | ☐ | |
| B-21 | `memblock_copy` 常量区间/元素类型不校验 | ☐ | |
| B-22 | 字面量专用码退化 | ☐ | |
| B-23 | 条件 RHS 码被 `CONDITION_TYPE` 覆盖 | ☐ | |
| B-24 | 常量 `cast` 字面量错误码 | ☐ | |
| B-25 | 子块/兄弟块标签优先级 | ☐ | |
| B-26 | 浮点非规格化被拒（过度拒绝） | ☐ | |
| B-27 | 格式说明符缓冲过窄 | ☐ | |
| B-28 | 类型嵌套解析崩溃 | ☐ | |
| B-29 | 数字分隔符规则被绕过 | ☐ | |
| B-30 | 尾随逗号（4 处） | ☐ | |
| B-31 | 缺失逗号（3 处） | ☐ | |
| B-32 | 嵌套构造器 | ☐ | |
| B-33 | 字段赋值 RHS 不接受 `funcall`（过度拒绝） | ☐ | |
| B-34 | `cast(void,…)`/`bitcast(void,…)` 未语法拒绝 | ☐ | |
| B-35 | `OPERAND_COUNT` 缺失 | ☐ | |
| B-36 | 格式标志重复与长度上限 | ☐ | |
| B-37 | 大写变量嵌套字段误解析（过度拒绝） | ☐ | |
| B-38 | `#lib` 裸 `var` 报 `MODULE_LAYER` | ☐ | |
| B-39 | SYN 阶段错位（`Self` 检查） | ☐ | |
| B-40 | SEM 码由解析器发出（`@padding`/`N` 来源） | ☐ | |
| B-41 | 深一级 `end` 缩进码归属 | ⊘ 待标准裁决 | |
| B-42 | `bitcast` 伪造指针槽索引越界 | ☐ | |
| B-43 | `memblock_copy` `length == 0` 跳过区间检查 | ☐ | |
| B-44 | `memblock_copy` 常量区间从不静态检查 | ☐ | |
| B-45 | strict `shl` 零被移位数不报溢出 | ☐ | |
| B-46 | `%f` 极小量错误进位 | ☐ | |
| B-47 | float32 字面量经 double 二次舍入 | ☐ | |
| B-48 | 浮点边界字面量被拒 | ☐ | |
| B-49 | `ptr_sub` `usize` 偏移有符号溢出 | ☐ | |
| B-50 | AOT `-r` 拒 `#lib`-only | ☐ | |
| B-51 | `ptr_address(T, 形参)` AOT 代码生成失败 | ☐ | |
| B-52 | strict 下溢按宿主 `FE_UNDERFLOW` | ☐ | |
| B-53 | `%.80d` 起报 `TC_RE_IO` | ☐ | |
| B-54 | 依赖模块裸名引用错码不一致 | ☐ | |
| B-55 | embed/工具链细节 | ☐ | |
| B-56 | `#lib` 内 `Self.f` 被 import 即失败 | ☐ | |

### B-57～B-66（审计报告 §2.12）

| 条目 | 主题 | 状态 | 提交 |
| ---- | ---- | ---- | ---- |
| B-57 | AOT 侧伪造指针非确定读 | ☐ | |
| B-58 | `memblock_copy` 空拷贝 dst 侧漏检 | ☐ | |
| B-59 | 常量负 dst 下标无静态检查 | ☐ | |
| B-60 | 格式越界且 Token >32 字节被降级 | ☐ | |
| B-61 | 非 `bool` 条件错码不一致 | ⊘ 待标准裁决 | |
| B-62 | `const` 列表产生式逗号 | ☐ | |
| B-63 | 顶层行缩进不校验 | ⊘ 待标准裁决 | |
| B-64 | 文件名含内部点致结构体解析失败 | ☐ | |
| B-65 | 跨模块同名符号致 CFG 假阳性 | ☐ | |
| B-66 | `static let` 经 `Self.` 引用规则不符 | ☐ | |

### §2.6 子项

| 子项 | 主题 | 状态 |
| ---- | ---- | ---- |
| 2.6.1 | 确定初始化 DFA 读集不完整（7 个场景） | ☑ |
| 2.6.2 | 操作数/字段类型只比较 `tag`（6 类） | ☑ |
| 2.6.3 | 名称冲突与作用域漏检（5 类） | ☑ |
| 2.6.4 | 控制流/可达性漏检（`while true` 后不可达；`else if`） | ☑ |
| 2.6.5 | `#lib` 顶层可执行语句未拒绝 | ☑ |
| 2.6.6 | `isize` 被当作 `usize` | ☑ |

### 阶段 2 逐条记录

| 条目 | 改动 | 验证 |
| ---- | ---- | ---- |
| B-1 | `tc_func_check.c`：`position == 1 && is_void`（void 作值）由 `TC_CE_FUNCALL_POSITION` 改为 `TC_CE_FUNCALL_RESULT_TYPE`；非 void 返回类型与接收类型不符由 `TC_CE_FUNCALL_RESULT_TYPE` 改为 `TC_CE_TYPE_MISMATCH`（[语言标准 §8.2.3]、附录 B.4/B.12）。`funcall_result_type.tc` 注释更正；`run_expect_check_fail` 增可选错误码名断言（不新增注册行） | `tc-vm -e -c` 三例实测；`--filter funcall` 20/20；全量三层通过 |
| B-2 | `tc_analyzer.c` 新增 `tc_sem_salvage`/`tc_sem_diag_earlier`：Pass2（6a–8）失败后仍以**独立临时诊断**尝试阶段 12 调用图与阶段 11 CFG/确定初始化；阶段 11 失败后再补阶段 12。仅当后阶段诊断 (行,列) 更靠前时替换（[语言标准 §11] 第 2 条；依编译器标准 §1.3 第 4–8/11/12 阶段同属 SEM）。CFG 读集按槽位展开、调用图只需签名，故 salvage 安全。新增语料 `diag_priority_{recursion,unreachable}_before_name.tc`（VM 断言消息＋打印名，AOT 断言消息）；`test-map.md` 规模回填 1004 VM / 466 AOT | 两复现实测（Recursion 第 3 行、Unreachable 第 4 行胜出）；`--filter diag_priority` 21/21；全量三层通过 |
| B-3 | 重复格式标志：`tc_types.h` 的 `TcFormatFullSpec` 增 `flag_repeat`；`tc_types.c` 的 `tc_format_spec_parse` 对重复 `-`/`+`/`#` 与非连续第二段 `0` **不再返回 0**（附录 A `{ format_flag }` 形态合法），只置 `flag_repeat`；`tc_analyze_6e.c` 在 SEM 阶段报 `TC_CE_FORMAT_SPECIFIER`（duplicate format flag）。附带修 AOT 代码生成：`(TcFormatFullSpec){...}` 改**指定初始化器**，避免新增字段触发 `-Wmissing-field-initializers`。新增语料 `format_duplicate_flag.tc`（VM 断言消息＋打印名，AOT 断言消息）与 4 条单元断言；test-map 回填 1005 VM / 467 AOT | `%--d`/`%++d`/`%##x`/`%0-0d` → FormatSpecifierError；`%-+d`/`%08d` 接受；`%8-d` 仍 SYNTAX；`--filter format` 47/47；全量三层通过 |
| B-4 | `tc_analyzer_pass2.c` 的 `let`（ConstDef）路径在 `tc_type_check_rhs` **之前**增加 §5.2.1 第 3 步预检：RHS 为裸标识符且绑定是 `var`/形参/`static var` → 立即 `TC_CE_CONSTANT_EXPRESSION`，不比较结果类型（`Self.`/限定名仍走 6d 常量来源规则）。新增语料 `let_var_type_mismatch.tc`（声明类型与 var 不同，锁定次序）；test-map 回填 1006 VM / 468 AOT | `let a: int8 = v`（v: int32）→ ConstantExpressionError（原 TypeMismatch）；同类型 var、形参、`var` 初始化/赋值对照均正确；全量三层通过 |
| B-5 | `tc_memblock_check.c`：`tc_memblock_resolve_usize_name` 只接受 `TC_USIZE`（不再放行 `isize`），类型不合法与数学值 < 1 统一报 `TC_CE_CONSTANT_EXPRESSION`（原分别错报 `TYPE_MISMATCH` / `MEMBLOCK_ELEMENT_COUNT_MISMATCH`）；构造器字面量 `count: 0` 同改为 `CONSTANT_EXPRESSION`（`ELEMENT_COUNT_MISMATCH` 仅保留给「逐值数量 ≠ count」）。新增语料 `memblock_count_source_isize.tc`；`test_type_check` 的 count-zero 期望更正；test-map 回填 1008 VM / 469 AOT | isize/int32/`usize=0` 的 `N` 与 `count:` 全部报 ConstantExpressionError；字面量 0（类型位与 count: 位）同；逐值数量不符仍报 MemblockElementCountMismatch；`--filter memblock` 73/73；全量三层通过 |

---

| B-6 | `tc_func_check.c`：删除 `tc_check_funcall_args` 中置于名称检查之前的 `arg_count > sig->param_count` 早退分支，改由「全部名称已知」处的检查承担（编译器标准 §8.2 第 3 条的 重复 → 未知 → 缺失 → 数量超限 → 顺序 → 类型）；语料 `funcall_extra_arg.tc` 更名 `funcall_count_exceeds.tc`（重复 `a` + 未知 `z` + 个数超限 → 报 DuplicateArgument），`unknown_argument.tc` 改为「个数超限但含未知名」（→ UnknownArgument），二者注册补错误码断言（不新增注册行，计数不变） | `funcall(Self.sum, a: 1, a: 2, z: 3)` → `DuplicateArgument: duplicate argument 'a'`（原 ExtraArgument）；`funcall(Self.sum, z: 1, y: 2)` → `UnknownArgument: unknown argument 'z'`；重复/未知/缺失/顺序四个既有语料码不变；注：按此次序 `EXTRA_ARGUMENT` 在「全名已知且无重复」下结构上不可达，实现保留该分支；全量三层与 5 项门禁通过 |
| B-7 | `tc_executor.c` `tc_exec_load_binding`：取槽位后按**使用点静态类型**重建值（`tc_value_make`），仅在「静态类型标签 ≠ 槽位内标签」且两者皆为非聚合（`tc_type_bit_width > 0`）时执行；`tc_eval_operand` 的 `TC_OPERAND_VAR` 兜底路径保持原样（该路径无 `binding->type` 一致性校验，按 `expected_type` 强行重建曾使 `memcopy_unsafe` 的 `int32 -1` 下标被当成 64 位无符号值而越界触发 SIGBUS）。新增 `tests/valid/ptr_alias_write_render_type.tc`（审计复现）并注册 VM stdout/check + AOT diff/check；test-map 回填 1010 VM / 471 AOT | 审计复现（`ptr_store(int64, cast(ptr<int64>, ptr_address(float64, f)), 7)` 后 `writeln(float64, f)`）VM 与 AOT 均输出 `3.45846e-323`（原 VM 输出 `7`）；`--filter memcopy_unsafe`、`--filter ptr_` 与全量三层通过；`tests/errors/runtime/memcopy_unsafe_neg_var_index.tc` 仍报 invalid range |
| B-8 | `tc_ptr_check.c` 新增 `tc_ptr_check_memcopy_unsafe_operands`（`tc_memblock_check.c` 在 void/只读检查后调用，`tc_memblock_check_memcopy_unsafe` 增传 `struct_table`）：`dst`/`src` 统一走指针操作数校验，既校验 `ptr<T>` 形式与所指类型是否等于显式 `T`，也解析结构体字段读取（`s.p`）并写入 operand。新增 `tests/valid/memcopy_unsafe_struct_field_ptr.tc`（审计复现）并注册 VM stdout + AOT diff；test-map 回填 1011 VM / 472 AOT。顺带闭合 B-20 的两个实测例：非指针操作数与 `T` 不符均静态报 `TC_CE_TYPE_MISMATCH`（B-20 余下的 `dst_idx`/`src_idx`/`length` 整数类型校验另做） | 审计复现 VM 与 AOT 均输出 `7`（原 VM 报 `unresolved field operand` 内部错误、AOT 报 null pointer dereference）；`memcopy_unsafe(int32, x, 0, y, 0, 1)` 与 `memcopy_unsafe(float32, p, 0, q, 0, 1)` 静态 TypeMismatch；`nullptr` 仍静态通过、运行时 NullPointerDereference；`--filter memcopy_unsafe` VM/AOT 全通过；全量三层与 5 项门禁通过 |
| B-9 | `tc_executor.c` `tc_eval_operand` 无名解析兜底：判据由 `sym->slot >= 0` 放宽为 `sym->slot >= 0 \|\| sym->has_const_value`（`let`/`static let` 的 slot 为 -1，原判据恒落 `tc_exec_load_binding` 的「unresolved binding metadata」内部错误）；新增 `tests/valid/memblock_copy_let_index.tc`（审计复现 + `memcopy_unsafe` 同形用例），注册 VM stdout + AOT diff；test-map 回填 1012 VM / 473 AOT | 审计复现 `memblock_copy(int32, d, I, s, I, 1)`（`let I: int32 = 0`）VM 与 AOT 均输出 `1`（原 VM 报内部错误）；同文件 `memcopy_unsafe(int32, p, I, q, I, 1)` 亦正常；全量三层与 5 项门禁通过 |
| B-10 | 执行期非法槽编码（§1.3 实现定义清单第 4 项）统一按 AOT 既有口径报用户可见运行期码，删除实现内部错误：`tc_ptr_exec.c` `tc_exec_ptr_load`/`tc_exec_ptr_store` → `TC_RE_NULL_POINTER_DEREFERENCE`，`tc_exec_ptr_arith` → `TC_RE_NULL_POINTER_ARITHMETIC`，`tc_memblock_exec.c` `memcopy_unsafe` → `TC_RE_NULL_POINTER_DEREFERENCE`。新增 `tests/errors/runtime/ptr_bitcast_forged_load.tc`（审计复现）与 `ptr_bitcast_forged_arith.tc`，注册 VM fail+check_ok、AOT runtime_fail；test-map 回填 1016 VM（AOT 注册计数只统计 diff/check_ok/check_fail/CLI golden，`run_runtime_fail` 不计入，保持 473） | 审计复现 `ptr_load` 的 VM 由「internal error: invalid pointer value」改为 `NullPointerDereference: null pointer dereference`，与 AOT 一致；`ptr_store`、`ptr_add` 两个同源路径同样一致（VM NullPointerArithmetic / AOT null pointer arithmetic）；`--filter ptr_bitcast_forged` 通过；全量三层与 5 项门禁通过。注：「解引用任意 usize 伪造的编码」在标准中属空白（审计列为 A 类），本轮按 §1.3 一致性要求对齐 AOT，不改标准 |
| B-11 | `tc_struct_exec.c`：结构体基堆句柄为空（经指针别名写入清零）时，字段读取改为按字段类型取 0 位模式（新增 `tc_exec_null_field_value`，覆盖 `tc_exec_eval_field_access` 与未解析回退路径），字段赋值改为 no-op（RHS 仍求值），与 AOT `tc_aot_struct_load_bits`/`tc_aot_struct_store_bits`/`tc_aot_struct_extract` 对空基址的行为一致；补 `tc_semantics.h` 引用。新增 `tests/valid/struct_alias_zeroed_field_read.tc`（审计复现），注册 VM stdout+check_ok、AOT diff；test-map 回填 1018 VM / 474 AOT | 审计复现 VM 与 AOT 均输出 `0`/`0`/`0`（原 VM 第二行起报「internal error: invalid struct field read」）；补测字段赋值（no-op）与嵌套结构体字段读取两后端一致（`0`/`0`/`1`）；`--filter struct_alias_zeroed` 通过；全量三层与 5 项门禁通过 |
| B-12 | `tc_module.c` `tc_module_check_import_name_conflict`：`TC_STMT_STRUCT_DEF` 移出 `#lib` 专属分支，两种模式都参与冲突检查（编译器标准 §4.6 名称冲突检查范围本轮已含 `#program` 的 `struct` 名，语言标准 §3.9.1/附录 B.2 本就无模式限定）；新增 `tests/errors/module/import_name_conflict_program_struct.tc`，并为 program/lib 两条既有用例补错误码断言；test-map 回填 1019 VM | 审计复现 `#program` + `import Foo` + `struct Foo` 现报 `ImportNameConflict`（VM 与 AOT 一致，原为接受）；`#lib` 三种形态仍正确拒绝；`--filter import_name_conflict` 4/4；全量三层与 5 项门禁通过 |
| B-13 | 新增 `tc_compile_source_opts`（`tc_compile_source` = opts 为 NULL 的等价形式）；分析器新增 `tc_analyze_memory(program, out, display_name, entry_module_name, search, diag)` 与 `tc_module_resolve_imports_ex`（入口模块名可显式给出，空串 = 无模块名），内存入口与文件入口同跑 4b–4d；`name` 所在目录为导入搜索第一候选，`.tc` 结尾的 `name` 由主干推导入口模块名（自导入可判），其它显示名保持入口无模块名以兼容 `tc_embed_call(ctx, NULL, …)`。同步改写 `docs/libtc设计说明书-0.0.44.md` §1/§2.1/§3.1/§7.3/§15.2/§15.3/§15.11/§15.12（阶段覆盖差异 → 两入口同阶段范围，C-19 的旧表述被本提交取代）；`tests/unit/runtime/test_libtc.c` 新增 `test_source_entry_resolves_imports`（缺失模块 / 自导入 / 无搜索路径 / 经 opts 解析四态） | 审计四项探针（链接 libtc.a）：`import NoSuchModule`（裸用/带成员引用）→ `ImportNotFound`；`#lib + import SelfLib` → `CircularImport`；带 `-I tests/modules/extra_libs` 的 `import ExtraLib` → 编译并运行成功（原全部 rc=0 接受）；`test-libtc` 103/103、`test-embed` 590/590（入口模块名语义未变）；全量三层与 5 项门禁通过 |
| B-56 | `TcFuncCheckEnv` 增 `module_index`（当前被分析模块在 `sigs` 中的下标）与 `tc_func_env_module_index`；`tc_func_resolve_call_target` 的 `Self.<函数名>` 分支改用 `env->module_index`（原硬编码 -1）；`tc_analyzer.c` 的依赖 Pass2 循环为每个 dep 现场构建成员索引并在调用期间切换 `func_env.members` / `module_index`（调用后恢复入口上下文），使 `Self.` 名称作用域与函数解析都按**当前模块**判定。新增 `tests/modules/SelfCallLib.tc` + `import_self_call.tc`（VM stdout+check_ok、AOT diff）与 `tests/modules/self_call_neg/{BareCallLib.tc,import_bare_call.tc}`（VM check_fail，断言 FunctionScopeAccessError）；test-map 回填 1022 VM / 475 AOT | 审计复现：`M2.tc` 作依赖时 `funcall(Self.f, …)` 由 `UndefinedFunction` 变为编译通过，入口 `funcall(M2.g, a: 5)` VM 与 AOT 均输出 `5`；模块作入口仍通过；依赖内裸名 `f` 由 `UndefinedFunction` 变为 `FunctionScopeAccessError`（与入口口径一致）；`Self.zzz` 仍 `UndefinedFunction`、跨模块 `M4.p` 仍 `PrivateMemberAccessError`；全量三层与 5 项门禁通过 |
| B-14 | `TcProgram` 增 `source_text`（源文本，模块阶段诊断片段用；`tc_program_init/free` 同步）；新增 `tc_diagnostic_use_source(diag, file, source)`（相同则跳过，避免整篇源码反复复制）；`tc_module.c` 在读取模块文件后把诊断定位切到模块自身，`tc_collect_imports_recursive` 每条 import 前切到写出该 import 的模块，`tc_module_resolve_imports_ex` 成功返回前恢复入口定位（`goto done` 统一释放）；`tc_analyzer.c` 在入口 move 后从诊断对象补记入口源文本，并新增 `tc_diag_use_module`，在结构体注册/Pass1/static let/static var/Pass2/CFG 六个依赖阶段逐 dep 切换、阶段结束后切回入口。新增 `tests/modules/BadLibDiag.tc` + `import_badlib_diag.tc`（VM check_fail，断言 `BadLibDiag.tc:5: error: undefined variable 'zzz'` + `UndefinedVariable`）；test-map 回填 1023 VM | 审计两例：`Bad3.tc` 的 `static let` 未定义变量报 `Bad3.tc:2: error [UndefinedVariable]` 且片段为模块第 2 行（原为入口文件 + 入口片段）；模块第 9 行的语法错误报 `Bad4.tc:9:5` + 正确片段（原为入口文件 + 模块行号 + 入口片段）；模块函数体内 Pass2 错误同样定位到模块；`import_badlib_uninit` 的 CFG 阶段诊断仍报模块文件且现附片段；全量三层与 5 项门禁通过 |
| B-15 | `src/aot/main.c` 非 Windows 分支：执行生成的可执行文件时，无目录分量的路径统一加 `./` 前缀（`run_path`），`-o` 仍用原路径；Windows `.bat` 分支不变（cmd.exe 默认搜索当前目录）。`scripts/aot/run_tests.sh` 增 `run_aot_relative_run`（临时目录内以裸相对名 `rel.tc` 跑 `-r`，校验 rc=0 与 stdout `1`；自定义 helper 不进入 `check_doc_counts` 的 AOT 注册计数） | 复现（`cd /tmp && tc-aot -r w38b.tc`）由 `sh: w38b.c.out: command not found`（exit 32512）变为正常执行；`sub/w38b.tc` 带目录分量相对路径与绝对路径均正常；全量三层与 5 项门禁通过 |
| B-16 | `tc_module.c`：`tc_read_file_text` 的 `fseek`/`ftell`/`fread` 失败改报 `TC_DIAG_API` / `TC_API_ERR_FILE_READ`（I/O 前先把定位切到该模块文件），并补 `nread != size` 短读检查；`tc_file_exists` 用 `stat` + `S_ISREG` 判定「模块文件」，目录等非普通文件不再被当作源码读入（原表现为入口文件上的 `expected #program or #lib`）。`tests/unit/runtime/test_libtc.c` 新增 `test_module_directory_is_not_a_module_file`（运行期构造 `DirMod.tc/` 目录 + 入口，断言语言域 `TC_CE_IMPORT_NOT_FOUND`） | 目录作导入目标：VM/AOT 均由 `error: expected #program or #lib`（定位入口、无行号）改为 `imp_dir.tc:1: error [ImportNotFound]: import module not found`；`test-libtc` 109/109；全量三层与 5 项门禁通过。注：`fseek`/`ftell`/短读分支无法用常规文件稳定触发，按 §1.3/§11.4 直接改为 API 域 |
| B-17 | `tc_parser_stmt.c`：`tc_parse_static_def` 不再用 `tc_expect_token(TC_TOK_EQUAL)`，改为 peek 判定——缺 `=` 或 `=` 后无初始化器且为 `static var` → `TC_CE_VAR_MISSING_INIT`（`static let` 维持「常量定义必须初始化」语法错误，与 `#program` 的 `let` 一致）；同时按编译器标准 §1.4 位置表把 `TC_CE_VAR_MISSING_INIT` 的主位置从「应有的 `=`/后继 token」改为**该声明的标识符**（`var`/`let` 与 `static var` 三条路径一致）。新增 `tests/errors/static/static_var_missing_initializer.tc`（VM fail_msg + check_fail 带码断言），既有 `var_missing_initializer.tc` 补码断言；`tests/unit/parser/test_parser.c` 新增 `test_parse_static_var_requires_initializer`（含 `static let` 仍为语法错误一例）；test-map 回填 1025 VM | `#lib / public static var V: int32` 由 `:2:27: error [SyntaxError]: unexpected token` 变为 `:2:19: error [VarMissingInitializer]: variable definition requires initializer`（VM 与 AOT 一致，列号落在标识符 `V`）；`public static var V: int32 =` 同码；`#program` 的 `var x: int32` / `var x: int32 =` 码不变、列号由 8 改为标识符列 5；`static let` 缺初始化器仍为 SyntaxError；test-parser 167/167；全量三层与 5 项门禁通过 |
| B-18 | `tc_diagnostic.c` 打印器：`TC_DIAG_IMPLEMENTATION` 域仅在 kind 为实现专用码（`kind >= TC_ERR_OUT_OF_MEMORY`，当前仅 `OutOfMemory`）时打印码名，实现缺陷（占位 kind 为 `TC_CE_SYNTAX`）只打印 `implementation error: <message>`，不再伪造语言码 `SyntaxError`；`tc_exec_set_internal_error` 补注释说明占位 kind 不被打印。文档同步：`TC-VM命令行参考` §2.2 与 `libtc设计说明书` §7.5 的输出格式、`TC-VM详细设计说明书` §1 的「既有未关闭差异」段落（删除已由 B-6/B-8/B-9/B-17 关闭的条目，改为中性表述并写明实现域不附语言码）。`tests/unit/runtime/test_diagnostic.c` 增两断言（实现缺陷打印不含语言码） | 62 处 `tc_exec_set_internal_error` 的输出由 `<file>:<line>: implementation error: SyntaxError: internal error: …` 变为 `<file>:<line>: implementation error: internal error: …`；`OutOfMemory` 仍打印 `implementation error: OutOfMemory: …`（test-diagnostic 53/53）；全量三层与 5 项门禁通过。注：本轮同时消除了 B-8/B-9/B-10/B-11 四类可由合法程序触发的内部错误，剩余内部错误点不再以语言码暴露 |
| B-19 | 新增 header-only `src/vm/runtime/tc_embed_slots.h`（`TC_EMBED_TMP_MIN=16` + `tc_embed_slot_capacity_of`），VM/AOT/宿主编译期共用同一容量口径；`TcEmbedCtx` 增 `slot_capacity`，VM 模式按容量分配槽位数组、AOT 模式由 `tc_embed_create_aot` 按声明槽位数计算容量，`tmp_top` 初值为容量；`tc_embed_tmp_begin` 分配不得越过声明槽位边界，`tc_embed_slot_write/read` 上界改为容量；`tc_aot_codegen.c` 嵌入模式的 `slots[]`（头文件声明 + 定义）按容量定长并新增 `TC_AOT_SLOT_CAPACITY`（非嵌入程序仍按声明槽位数定长）；`scripts/sync/check_source_naming.py` 把新头文件加入 header-only 白名单。文档同步 `TC-Embed详细设计说明书` §16.4（含 AOT 定长与不变量）。测试：`test_embed_tmp_begin_end` 改为断言临时区起点 ≥ 声明槽位数、满额分配止于边界、`reserve+1` 被拒；新增 `test_embed_tmp_region_does_not_clobber_declared_slots`（先写满声明槽位 → `make_ptr` 平铺 4 元素 → 逐槽回读不变） | `tc_embed_make_ptr` 平铺不再覆盖已声明槽位（新增用例在旧实现下必然失败）；容量示例：声明 5 槽 → `TC_AOT_SLOT_COUNT 5` / `TC_AOT_SLOT_CAPACITY 21` / `slots[21]`；`test-embed` 601/601、`test-embed-aot` 383/383；全量三层与 5 项门禁通过 |
| B-20 | 新增通用 `tc_check_integer_operand`（`tc_analyzer_pass2_rhs.c`，接受任意整数宽度/符号性：字面量须为整数字面量，标识符/限定名/字段读取须解析为整数类型并写回 binding/字段解析）；`tc_ptr_check_memcopy_unsafe_operands` 在 `dst`/`src` 之后依次校验 `dst_index`/`src_index`/`length`（§6.8.9 操作数表）；`tc_memblock_exec.c` 的 `tc_memblock_read_index_kind` 改为按**声明的整数类型**求值（绑定已解析时），否则 `tc_exec_load_binding` 会因元数据不匹配报内部错误。同时闭合审计 §5 既有-8：`memblock_load`/`memblock_store` 的 `index` 由「必须恰好 usize/isize」改为「任意整数类型」（§6.7.2.4）。新增 `tests/errors/static/memcopy_unsafe_index_operand.tc`（指针作下标）与 `memcopy_unsafe_length_operand.tc`（浮点 length）（VM check_fail + TypeMismatch），`tests/valid/memblock_index_int32.tc`（int32 下标，VM stdout+check_ok、AOT diff）；test-map 回填 1029 VM / 476 AOT | 审计两例（非指针 `dst`/`src`、`T` 与所指不符）已由 B-8 闭合；本轮补：指针作 `dst_idx`、浮点 `length`、`bool` 作下标 → `TypeMismatch: index operand must be an integer`（原全部接受）；`memcopy_unsafe` 的 `let I: int32` 下标两后端输出一致；既有-8 `var i: int32 = 1` 作 `memblock_load`/`memblock_store` 下标由 `TypeMismatch` 变为正常执行（VM/AOT 均输出 5、9）；全量三层与 5 项门禁通过 |
| B-21 | `tc_memblock_check_copy` 增 `struct_table` 参数；新增 `tc_memblock_const_index_value`（区分非常量 / 非负常量 / 负常量），对 `dst_index`/`src_index`/`length` 先做整数 operand 校验（`tc_check_integer_operand`，解析写回 binding）再做常量区间判定：负常量下标、常量 `length < 0`、常量区间越界（`length > 0` 时要求 `index + length ≤ count`；`length` 为 0 或不可确定时只拒绝 `index > count`，空拷贝允许 `index == count`）→ 静态 `TC_CE_MEMBLOCK_INDEX_OUT_OF_RANGE`；元素类型检查既有。新增 `tests/errors/static/memblock_copy_constant_range.tc`（length 3 > count 2）与 `memblock_copy_negative_length.tc`（length -1）（VM check_fail + MemblockIndexOutOfRange），`tests/valid/memblock_copy_empty_at_end.tc`（空拷贝 index == count，VM stdout+check_ok、AOT diff）；test-map 回填 1033 VM / 477 AOT | 审计表三例：常量越界 `memblock_copy(int32, b, 0, a, 0, 3)`（N=2）与 `length = -1` 由 `-c` 通过变为静态 `MemblockIndexOutOfRange`；`memblock_copy(float32, …)` 仍为静态 `TypeMismatch`；空拷贝 `index == count` 仍接受；变量 length 越界仍留运行时（两后端一致）；全量三层与 5 项门禁通过 |
| 2.6.6 | 六处「usize 结果类型」接受 `isize` 的检查统一收紧为 `!= TC_USIZE`：`tc_memblock_check.c`（`.count` RHS）、`tc_struct_check.c`（字段 `.count`）、`tc_ptr_check.c`（`ptr_size`）、`tc_const_eval.c`（静态布尔取值的 `.count` 原子、常量 `.count`、常量 `ptr_size`），错误消息同步为 `must be usize`。新增 `tests/errors/static/memblock_count_isize.tc` 与 `ptr_size_isize.tc`（VM fail_msg + check_fail/TypeMismatch），并更新 `memblock_count_type.tc` / `ptr_size_not_usize.tc` 的注释与注册串；test-map 回填 1037 VM | `var c: isize = m.count` 与 `let w: isize = ptr_size(int32, nullptr)` 由 ACCEPT 变为 `TypeMismatch`（VM/AOT 一致）；`var c: usize = m.count` 仍正常（输出 3）；`var n: int32 = mb.count` / `var n: int32 = ptr_size(…)` 仍报同码；全量三层与 5 项门禁通过 |
| 2.6.5 | `tc_parser.c` `tc_parse_module_body`：`#lib` 模式下把 EXEC 层行按语法拒绝（`TC_CE_SYNTAX`，主位置为该行首 Token），例外放行 `goto`/`label`/`break`/`continue`/`return` —— 附录 A 明示其顶层出现由后续静态语义报专用码（保住既有 `GotoOutsideFunction` 等诊断）。新增 `tests/errors/static/lib_toplevel_statement.tc`（`public static var W` + 顶层 `writeln`），注册 VM check_fail（SyntaxError）与 fail_msg；test-map 回填 1039 VM | 审计复现：`#lib` + 顶层 `writeln(int32, 42)` 由 ACCEPT（被 import 时静默丢弃）变为 `SyntaxError: executable statement is not allowed in #lib`（VM/AOT 一致）；`#lib` + 顶层 `if` 同样拒绝；顶层 `goto`/`return`/`break`/`continue` 仍报各自专用码；`#lib` 顶层 `var`/`let` 仍报 `ModuleLayerError`；合法 `#lib`（仅 static/func）不受影响；全量三层与 5 项门禁通过 |
| 2.6.3 | 三处补齐：①`tc_analyzer_pass1.c` 新增 `tc_pass1_param_name_conflict`，`TcAnalyzeCtx` 增 `current_params`/`current_param_count`（在 FUNC_DEF 分支设置并恢复），`var`/`let` 在**任意层级**与形参同名 → `DUPLICATE_DEFINITION`（不能扫全符号表：符号表跨函数/模块共享，已弹栈的形参仍在）；②`tc_analyzer_pass2.c` 新增 `tc_pass2_check_nested_func_name_conflict`（按当前模块成员索引判定），函数体任意层级/顶层嵌套块的 `var`/`let` 与全局函数同名 → `FUNCTION_NAME_CONFLICT`；③`tc_struct_check.c` 结构体注册时扫描同模块 `func`/`var`/`let`/`static var`/`static let` 名，冲突 → `FUNCTION_NAME_CONFLICT`（主位置取两者中源序更晚者）。新增四份语料：`param_shadow_nested.tc`、`nested_var_function_conflict.tc`、`struct_value_name_conflict.tc`、`struct_function_name_conflict.tc`（VM check_fail + 码断言）；test-map 回填 1043 VM（结构体名与导入名冲突已由 B-12 闭合） | 审计五个复现：`if true then var a`（与形参 `a` 同名）、函数体内 `var g`（与 `func g` 同名）、`struct S` + `var S`、`public struct F` + `public func F` 全部由 ACCEPT 变为对应专用码（VM/AOT 一致）；`#program` + `import pub` + `struct pub` 已由 B-12 拒绝；合法程序（如 `Util.tc` 的 `add1(x)` 被 import、`#program` 顶层 `var x` 与依赖形参同名）不受影响（test-module 65/65）；全量三层与 5 项门禁通过 |
| 2.6.4 | ①`tc_cfg.c` `tc_cfg_diagnose_unreachable` 的结构可达 BFS 跳过「恒真 LOOP_CONDITION 的 FALSE 出边」（`!edge->enabled && TC_CFG_FALSE && from->kind == TC_CFG_LOOP_CONDITION`）——`break` 走 TC_CFG_BREAK 边直达 LOOP_EXIT，故 `while true` 经 break 终止时其后语句仍可达；`while false` 的 TRUE 出边仍按结构可达处理（本轮只收敛恒真循环）。②`tc_parser.c` `if` 解析在匹配 `else` 后调用 `tc_expect_stmt_end`，`else` 行只允许 `else` 本身（+ 可选 `;`），单行 `else if` 报 `SyntaxError: unexpected trailing tokens`（附录 A `if_stmt` 要求 else 后换行 + 缩进 + suite；§7.1.1 不支持单行 `else if`）。新增 `unreachable_after_while_true.tc`、`else_inline_if.tc`（VM check_fail + 码断言）与 `while_true_break_reachable.tc`（VM stdout+check_ok、AOT diff）；test-map 回填 1047 VM / 478 AOT | `while true` 后语句由 ACCEPT 变为 `UnreachableStatement`（VM/AOT 一致）；`while true` + 可达 `break` 后语句仍接受且两后端输出 3；单行 `else if` 由「接受且 else 体无条件执行（打印 2）」变为 `SyntaxError`；合法多行 if/else 不受影响（输出 2）；全量三层与 5 项门禁通过 |
| 2.6.1 | ①`tc_cfg.c` `tc_cfg_add_rhs_reads` 展开全部复合/调用 RHS 读集（memblock_load/构造器/`.count`/结构体构造器/`ptr_*`/`funcall` 实参/`Self.<成员>`）；②语句层补读集：赋值（含**目标自身**，§6.2）、`return` 操作数、`funcall` 实参、`memblock_store`/`memblock_copy`/`ptr_store`/`memcopy_unsafe` 操作数、字段赋值基址与 RHS；③补全这些语句节点的诊断行号；④读集解析优先取 Pass2 已解析的 `binding.slot`（按名查找在多模块共享符号表 + 各模块 stmt_index 各自从 0 编号时会命中另一模块的同名绑定），按名入口跳过 `static` 域（静态成员不参与函数确定初始化）；⑤修 Pass1 形参 `def_stmt_index` 取**函数定义自身**序号（原取 body_start，使函数体首语句中的形参读取不可见并回退到同名符号——两个函数同名形参时误报）；`tc_cfg_collect_param_slots` 同步改按函数序号匹配。新增 8 份 `uninit_*` 语料（VM check_fail + UninitializedVariable）与 `tests/valid/dup_param_names_ok.tc`（VM check_ok、AOT check_ok）；test-map 回填 1056 VM / 479 AOT | 审计 7 个场景全部由 ACCEPT 变为 `UninitializedVariable`：赋值目标、`return x`、`funcall(Self.g, v: x)`、`memblock_load(int32, m, 0)`（m 被跳过）、`m.count`、`S(a: x)`、`memblock_store(int32, m, 0, x)`（另补 `ptr_store` 一例），行号均正确；同名形参的两个函数（`dup_param_names_ok.tc`）与既有 `nested_call`/`qualified_*`/`self_member_*`/`increment_static` 等不再误报；全量三层与 5 项门禁通过 |
| 2.6.2 | 新增 `tc_check_operand_strict`（`tc_analyzer_pass2_rhs.c`）：先按 `expected->tag` 做基础检查，期望类型为 `ptr`/`memblock`/`struct` 时再要求**完整同型**（`tc_type_equals` 递归比较所指/元素/struct_id + `tc_type_memblock_count_mismatch` 比较 `N`），操作数实际类型取自 `binding.type` 或字段解析结果。在三处 tag-only 比较点改用：结构体构造器字段值（`tc_struct_check.c`）、`memblock_store` 的 `value`（§6.7.2.2）、`ptr_store` 的 `value`（§6.8.3）。另在 `cast`/`bitcast` 源类型解析后、位宽判定前拒绝 `struct`/`memblock` 源（§6.6.6，`TC_CE_TYPE_MISMATCH`）。新增 7 份语料（构造器结构体/指针所指/N、memblock_store、ptr_store、cast、bitcast），VM check_fail + 码断言；test-map 回填 1063 VM | 审计 6 类：`C(a: b)`（A 字段给 B）、`P(p: pi)`（`ptr<float32>` 给 `ptr<int32>`）、`M(m: mb2)`（`memblock<int32,4>` 给 `memblock<int32,2>`）、`memblock_store(A, m, 0, b)`、`ptr_store(ptr<float32>, ppf, pi)`、`cast(int32, struct)`／`bitcast(int64, struct)` 全部由 ACCEPT（或错报 `BitcastWidthError`）变为 `TypeMismatch`；同型的构造器/存储用例仍通过（全量三层绿）；全量三层与 5 项门禁通过 |
## 需标准 owner 裁决（本轮跳过，不动语言标准）

| 条目 | 待裁决点 |
| ---- | -------- |
| A-1 | `ptr_address`/`ptr_add`/`ptr_sub` 同时被标为 RHS 与 `operand` |
| A-2 | `LITERAL_OUT_OF_RANGE` 阶段列不完整（LT / SEM） |
| A-3 | `memcopy_unsafe` 负下标属静态还是运行时 |
| A-4 | `bitcast(T, nullptr)` 源类型未定义 |
| B-41 | 深一级 `end` 的缩进码归属 |
| B-61 | `LITERAL_TYPE` 与 `CONDITION_TYPE` 优先级 |
| B-63 | 顶层行缩进是否合法 |

---

## 提交记录

| 序号 | 项 | 提交 |
| ---- | -- | ---- |
| 0 | 建立本台账 | `ca2db38 docs(0.0.44): add conformance remediation ledger and list it in the doc map` |
| 1 | 阶段 1-① 编译器标准 C-1～C-9 ＋ P2 | `47c6967 docs(0.0.44-compiler): align compiler specification with language spec (C-1..C-9 + P2)` |
| 2 | 阶段 1-② VM 详设 C-12～C-14 ＋ P2 | `9863b3a docs(0.0.44-vm): align VM design spec (C-12..C-14 residual + static-init stage)` |
| 3 | 阶段 1-③ AOT 详设 C-10～C-11 ＋ P2 | `01174dc chore(0.0.44-ledger): record AOT design-spec verification (no changes needed)` |
| 4 | 阶段 1-④ TC-Embed 详设 C-20～C-21 ＋ P2 | `8edb1db docs(0.0.44-embed): fix C-syntax TC examples and slot-overlap host code (C-20/C-21 + P2)` |
| 5 | 阶段 1-⑤ libtc 设计说明书 C-19 ＋ P2 | `f2b7328 docs(0.0.44-libtc): scope the memory entry, fix contract contradictions (C-19 + P2)` |
| 6 | 阶段 1-⑥ VM 命令行参考 C-15～C-18 ＋ P2 | `a570863 docs(0.0.44-cli): fix search-path/e/error-table and illegal examples (C-15..C-18 + P2)` |
| 7 | 阶段 1-⑦ 跨文档一致性 C-22～C-24 | `be56a6a docs(0.0.44-cross): unify sync-status notes and cross-doc definitions (C-22..C-24)` |
| 8 | 阶段 2-B1 `funcall` 位置码/结果码 | `096438b fix(0.0.44-B1): correct funcall position/result type diagnostics` |
| 9 | 阶段 2-B2 SEM 首个诊断按源序 | `42c3c0a fix(0.0.44-B2): order SEM diagnostics by source position` |
| 10 | 阶段 2-B3 重复格式标志归 SEM | `5f1dd2a fix(0.0.44-B3): report duplicate format flags as FORMAT_SPECIFIER` |
| 11 | 阶段 2-B4 `let` RHS 常量性先于类型 | `1af9045 fix(0.0.44-B4): check let RHS constness before type comparison` |
| 12 | 阶段 2-B5 memblock N/count: 来源与错码 | `34bd06f fix(0.0.44-B5): restrict memblock N/count sources to usize constants` |
| 13 | 阶段 2-B6 funcall 实参诊断次序 | `04e969e fix(0.0.44-B6): order funcall argument diagnostics per standard` |
| 14 | 阶段 2-B7 VM write/writeln 渲染类型 | `5ca2958 fix(0.0.44-B7): render write value by static operand type` |
| 15 | 阶段 2-B8 memcopy_unsafe 字段指针操作数 | `0e40d2b fix(0.0.44-B8): resolve struct field ptr operands in memcopy_unsafe` |
| 16 | 阶段 2-B9 顶层 let 作下标操作数 | `83c76fb fix(0.0.44-B9): evaluate const bindings in operand fallback` |
| 17 | 阶段 2-B10 非法指针编码两后端分歧 | `5ad9b34 fix(0.0.44-B10): report user-visible codes for invalid ptr encodings` |
| 18 | 阶段 2-B11 空结构体句柄字段读写 | `e467116 fix(0.0.44-B11): read fields from a null struct base as zeros` |
| 19 | 阶段 2-B12 `#program` 结构体名与导入名冲突 | `d7769d7 fix(0.0.44-B12): check struct names against import names in #program` |
| 20 | 阶段 2-B13 libtc 内存入口解析 import | `5240385 fix(0.0.44-B13): resolve imports from the in-memory libtc entry` |
| 21 | 阶段 2-B56 依赖模块内 `Self.<函数>` 调用 | `0e31475 fix(0.0.44-B56): resolve Self calls against the current module` |
| 22 | 阶段 2-B14 依赖模块诊断定位 | `d066adb fix(0.0.44-B14): locate dependency diagnostics in the module file` |
| 23 | 阶段 2-B15 `tc-aot -r` 相对路径 | `8a1cd5f fix(0.0.44-B15): run generated AOT binary via ./ for relative paths` |
| 24 | 阶段 2-B16 模块文件 I/O 错误域 | `9dc9c06 fix(0.0.44-B16): map module file I/O failures to the API domain` |
| 25 | 阶段 2-B17 `static var` 缺初始化器错码/定位 | `8199359 fix(0.0.44-B17): report VAR_MISSING_INIT for static var` |
| 26 | 阶段 2-B18 实现域诊断不附语言码 | `6e1073b fix(0.0.44-B18): stop printing language codes for implementation errors` |
| 27 | 阶段 2-B19 嵌入临时槽区不再重叠 | `73a7d53 fix(0.0.44-B19): place embed temp slots above declared slots` |
| 28 | 阶段 2-B20 整数下标操作数校验（含既有-8） | `718e66a fix(0.0.44-B20): check integer index operands` |
| 29 | 阶段 2-B21 `memblock_copy` 常量区间校验 | `c0bf20d fix(0.0.44-B21): check constant memblock_copy ranges statically` |
| 30 | 阶段 2-§2.6.6 isize 不得当作 usize | `cfc6601 fix(0.0.44-2.6.6): require usize for count/ptr_size results` |
| 31 | 阶段 2-§2.6.5 `#lib` 顶层可执行语句 | `5468e50 fix(0.0.44-2.6.5): reject top-level executable statements in #lib` |
| 32 | 阶段 2-§2.6.3 名称冲突与作用域漏检 | `3937432 fix(0.0.44-2.6.3): close name-conflict and scope gaps` |
| 33 | 阶段 2-§2.6.4 控制流/可达性漏检 | `1b49d12 fix(0.0.44-2.6.4): close reachability gaps (while true / else if)` |
| 34 | 阶段 2-§2.6.1 确定初始化读集补全 | `c04e53e fix(0.0.44-2.6.1): complete the definite-initialization read sets` |
| 35 | 阶段 2-§2.6.2 严格同型比较 | 本提交 `fix(0.0.44-2.6.2): compare full operand types, not just tags` |

---

*— 台账结束 —*

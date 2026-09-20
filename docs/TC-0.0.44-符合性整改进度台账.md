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
| B-12 | `#program` 结构体名 vs `import` 名冲突被放过 | ☑ | |
| B-13 | libtc 内存入口不解析 `import` | ☑ | |
| B-14 | 依赖模块诊断定位到入口文件 | ☑ | |
| B-15 | `tc-aot -r` 相对路径失败 | ☑ | |
| B-16 | 模块文件 I/O 失败映射为语言码 | ☑ | |
| B-17 | `#lib static var` 缺初始化器错码 | ☑ | |
| B-18 | `implementation error` 泄漏 | ☑ | 见文末提交记录 |
| B-19 | embed 临时槽区与声明槽区重叠 | ☑ | |
| B-20 | `memcopy_unsafe` 操作数类型不校验 | ☑ | |
| B-21 | `memblock_copy` 常量区间/元素类型不校验 | ☑ | |
| B-22 | 字面量专用码退化 | ☑ | |
| B-23 | 条件 RHS 码被 `CONDITION_TYPE` 覆盖 | ☑ | |
| B-24 | 常量 `cast` 字面量错误码 | ☑ | |
| B-25 | 子块/兄弟块标签优先级 | ☑ | |
| B-26 | 浮点非规格化被拒（过度拒绝） | ☑ | |
| B-27 | 格式说明符缓冲过窄 | ☑ | |
| B-28 | 类型嵌套解析崩溃 | ☑ | 见文末提交记录 |
| B-29 | 数字分隔符规则被绕过 | ☑ | 见文末提交记录 |
| B-30 | 尾随逗号（4 处） | ☑ | 见文末提交记录 |
| B-31 | 缺失逗号（3 处） | ☑ | 见文末提交记录 |
| B-32 | 嵌套构造器 | ☑ | |
| B-33 | 字段赋值 RHS 不接受 `funcall`（过度拒绝） | ☑ | |
| B-34 | `cast(void,…)`/`bitcast(void,…)` 未语法拒绝 | ☑ | |
| B-35 | `OPERAND_COUNT` 缺失 | ☑ | |
| B-36 | 格式标志重复与长度上限 | ☑ | |
| B-37 | 大写变量嵌套字段误解析（过度拒绝） | ☑ | |
| B-38 | `#lib` 裸 `var` 报 `MODULE_LAYER` | ☑ | |
| B-39 | SYN 阶段错位（`Self` 检查） | ☑ | |
| B-40 | SEM 码由解析器发出（`@padding`/`N` 来源） | ☐ | |
| B-41 | 深一级 `end` 缩进码归属 | ⊘ 待标准裁决 | |
| B-42 | `bitcast` 伪造指针槽索引越界 | ☑ | |
| B-43 | `memblock_copy` `length == 0` 跳过区间检查 | ☑ | |
| B-44 | `memblock_copy` 常量区间从不静态检查 | ☑（B-21 闭合） | |
| B-45 | strict `shl` 零被移位数不报溢出 | ☑ | |
| B-46 | `%f` 极小量错误进位 | ☑ | |
| B-47 | float32 字面量经 double 二次舍入 | ☑ | |
| B-48 | 浮点边界字面量被拒 | ☑ | |
| B-49 | `ptr_sub` `usize` 偏移有符号溢出 | ☑ | |
| B-50 | AOT `-r` 拒 `#lib`-only | ☑ | |
| B-51 | `ptr_address(T, 形参)` AOT 代码生成失败 | ☑ | |
| B-52 | strict 下溢按宿主 `FE_UNDERFLOW` | ☑ | |
| B-53 | `%.80d` 起报 `TC_RE_IO` | ☑ | |
| B-54 | 依赖模块裸名引用错码不一致 | ☑（B-56 闭合） | |
| B-55 | embed/工具链细节（4 项） | ☑（4/4） | |
| B-56 | `#lib` 内 `Self.f` 被 import 即失败 | ☑ | |

### B-57～B-66（审计报告 §2.12）

| 条目 | 主题 | 状态 | 提交 |
| ---- | ---- | ---- | ---- |
| B-57 | AOT 侧伪造指针非确定读 | ☑ | |
| B-58 | `memblock_copy` 空拷贝 dst 侧漏检 | ☑（含运行期分支覆盖补强） | |
| B-59 | 常量负 dst 下标无静态检查 | ☑（B-21 闭合） | |
| B-60 | 格式越界且 Token >32 字节被降级 | ☑（B-27 闭合） | |
| B-61 | 非 `bool` 条件错码不一致 | ⊘ 待标准裁决 | |
| B-62 | `const` 列表产生式逗号 | ☑（B-30/B-31 闭合） | |
| B-63 | 顶层行缩进不校验 | ⊘ 待标准裁决 | |
| B-64 | 文件名含内部点致结构体解析失败 | ☑ | |
| B-65 | 跨模块同名符号致 CFG 假阳性 | ☑（并修正串槽读值） | |
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
| B-22 | `tc_analyzer.c` `tc_check_literal`：字面量**类型**不匹配一律报 `TC_CE_LITERAL_TYPE`（忽略调用点传入的比较/条件类通用码；形参保留但标注未用），越界仍报 `TC_CE_LITERAL_OUT_OF_RANGE`。因 `tc_check_operand` / `tc_check_operand_strict` 是各操作数位置的公共入口，一处修改即覆盖内建运算、比较、I/O、构造器字段、memblock_store 等全部位置。新增 5 份语料（`literal_type_writeln/arith/compare/ctor/memblock_store`），VM check_fail + `LiteralTypeError` 断言；test-map 回填 1068 VM | 审计表：`writeln(int32, 1u)`、`add(int32, 1u, 1)`、`eq(int32, x, 1.5)`、`S(a: true)`、`memblock_store(int32, m, 0, 1.5)` 由 `TypeMismatch`/`ComparisonTypeMismatch` 全部变为 `LiteralTypeError`；`var a: int32 = 1u` 仍为 `LiteralTypeError`（对照正确）；全量三层与 5 项门禁通过 |
| B-23 | `tc_analyzer_pass2_rhs.c` 新增 `tc_rhs_kind_yields_bool`（比较/逻辑类 RHS 的静态结果类型为 bool）；`tc_check_condition` 仅在「结果类型非 bool」的形态（字面量/操作数/算术/调用等）上把 `TYPE_MISMATCH` 改写为 `TC_CE_CONDITION_TYPE`，结果类型本就是 bool 的比较类内部失败（如跨类型指针比较）保留其专用码。新增 `tests/errors/static/condition_ptr_type_mismatch.tc`，VM check_fail + TypeMismatch；test-map 回填 1069 VM | `if ptr_lt(int32, p, q)`（`ptr<int32>` vs `ptr<uint8>`）由 `ConditionTypeError` 变为 `TypeMismatch`，与 `var b: bool = ptr_lt(…)` 位置一致（§7.1.1 + 附录 B.11）；`if x then`（x: int32）仍报 `ConditionTypeError`（`if condition must be bool`）；全量三层与 5 项门禁通过 |
| B-24 | `tc_const_eval.c` 常量 `cast` 的字面量源检查改用 `tc_check_literal(..., TC_CE_LITERAL_TYPE)`（原为 `tc_literal_fits_context` + `TC_CE_CONSTANT_EXPRESSION`），使越界字面量报 `TC_CE_LITERAL_OUT_OF_RANGE`（§6.6.1.1 / §5.2.1 第 2 步 / 附录 B.6）。新增 `tests/errors/static/const_cast_literal_out_of_range.tc`，VM check_fail + LiteralOutOfRange；test-map 回填 1070 VM | `let a: uint64 = cast(uint64, 18446744073709551615)` 由 `ConstantExpressionError` 变为 `LiteralOutOfRange`，与 `var` 形式的同一表达式一致；不带 `cast` 的 `var a: uint64 = 18446744073709551615` 仍按上下文定型接受；全量三层与 5 项门禁通过 |
| B-25 | `tc_analyzer_pass2.c` `tc_resolve_goto_label` 增 `best_child` 候选：label 深度大于 goto 且 goto 路径是其前缀（跳入子块）时单列，返回次序改为 同级 → 祖先 → **子块** → 兜底 `any`。构造等价复现（§7.3.2 步骤 4 先于步骤 5）：同一函数内经兄弟块重名标签，子块标签先注册、兄弟块标签后注册，原实现因表序取 `any` 报 `JumpIncompatibleBlockError`。新增 `tests/errors/static/goto_label_child_vs_sibling.tc`（VM check_fail + JumpIntoBlockError）；test-map 回填 1071 VM | 复现构造由 `JumpIncompatibleBlockError` 变为 `JumpIntoBlockError`；兄弟块标签独存时仍报 `JumpIncompatibleBlockError`；同级/祖先标签与既有 goto 语料全部不受影响；全量三层与 5 项门禁通过。注：审计标注本条为分代理结论、未逐字复现，本轮已构造等价复现并修正 |
| B-26 | `tc_lexer.c` 浮点字面量：①`strtod` 的 `ERANGE` 不再一律判失败，仅在「舍入为零（`value == 0.0`）」或「上溢为无穷（`isinf`）」时报 `TC_CE_LITERAL_OUT_OF_RANGE`（§2.4.1）；②`float32_suffix` 的舍入判据由 `< 2^-149` 改为「舍入到零的分界」`< 2^-150`。新增 `tests/valid/float_denormal_literals.tc`（float64 5e-324/1e-308、float32 1e-45f；VM stdout + check_ok、AOT diff）与 `tests/errors/static/float_rounds_to_zero.tc`（float32 1e-46f → LiteralOutOfRange）；test-map 回填 1074 VM / 480 AOT | 审计表五项全部符合：`float64 = 1e-308`、`5e-324`、`2.3e-308` 与 `float32 = 1e-45f` 由拒绝变为接受（VM/AOT 输出一致：`4.94066e-324` / `1e-308` / `1.4013e-45`）；`float32 = 1e-46f`（舍入为零）与 `float64 = 1e400`/`1e-400` 仍拒绝；全量三层与 5 项门禁通过 |
| B-27 | `tc_lexer.c` 格式说明符词法缓冲由 32 改为 64 字节；标志扫描按 §10.5「连续 `0` 合并」规则折叠连续的 `0`（只保留一个），使 `%` + 大量 `0` + 宽度这一合法形态不再触发词法上限。原语料 `format_specifier_too_long.tc` 更名 `format_width_out_of_range.tc` 并改断言为 `format width or precision out of range` + `FormatSpecifierError`（宽度超出 §10.5 范围本属 SEM 专用码，位数不再被词法截断）；VM/AOT 注册串同步。新增 `tests/valid/format_flag_zero_merge.tc`（`%` + 40 个 `0` + `8d`，等价 `%08d`；VM stdout+check_ok、AOT diff）；test-map 回填 1076 VM / 481 AOT | 审计复现 `%` + 40 `0` + `8d` 由 `SyntaxError: format specifier too long` 变为接受并输出 `00000005`（两后端一致）；`%` + 40 `-` + `8d` 报 `FormatSpecifierError: duplicate format flag`（保留 B-3 的 SEM 码，不再被词法上限遮蔽）；30 位宽度报 `FormatSpecifierError: format width or precision out of range`；全量三层与 5 项门禁通过 |
| B-28 | `tc_parser_type.c`：`tc_parse_type_syntax` 拆出带深度参数的 `tc_parse_type_depth`（公开入口以 depth=0 转发），`ptr<…>` 与 `memblock<…>` 两处递归前检查 `depth >= TC_PARSER_MAX_DEPTH`（256，与 RHS 解析同一上限），超限报 `TC_CE_SYNTAX: type nesting too deep`。新增 `tests/errors/static/type_nesting_too_deep.tc`（300 层 `ptr<…>`，VM check_fail + SyntaxError）；test-map 回填 1077 VM | 审计复现 `ptr<`×50000 与 `memblock<`×30000 由 SIGSEGV（rc=139）变为 `SyntaxError: type nesting too deep`；100 层 `ptr<…>` 仍正常接受；全量三层与 5 项门禁通过 |
| B-29 | `tc_lexer.c` `tc_parse_radix_digits`：`prev_underscore = 0` 移到「确认当前字符是合法数字」之后——原实现在数字判定前就清除该标志，使下划线后跟非数字（`u`/`U` 后缀或行尾）不被识别为「字面量尾部下划线」。新增 `literal_trailing_underscore.tc`（`1_u`）与 `literal_trailing_underscore_hex.tc`（`0x1F_U`），VM check_fail + SyntaxError；test-map 回填 1079 VM | 审计五例：`1_u`、`1_`、`0x1F_U`、`0b1_u`、`0o7_u` 由 ACCEPT 变为 `SyntaxError: invalid integer literal`；合法分隔符 `1_000` 仍接受；连续下划线 `1__0` 仍拒绝；全量三层与 5 项门禁通过 |
| B-30 / B-31 | 四处列表产生式统一为「项之间必须有逗号、末项后不得有逗号」：`tc_parser_func.c` 形参表、`tc_parser_stmt.c` 两处命名实参表（funcall 语句 / RHS 表达式位置）、`tc_parser_rhs.c` 的 memblock 逐值构造器与结构体构造器。实现方式：消费逗号后若紧随 `)` 则报尾随逗号；无逗号且下一 Token 非 `)` 则报「expected , or )」。新增 8 份语料 `list_trailing_comma_{param,args,ctor,memblock}.tc` 与 `list_missing_comma_{param,args,ctor,memblock}.tc`（VM check_fail + SyntaxError）；test-map 回填 1087 VM | 审计 4 处尾随逗号（`f(a: int32,)`、`funcall(Self.g,)`、`S(a: 1, b: 2,)`、`memblock(int32, count: 2, 1, 2,)`）与 3 处缺失逗号（形参表 / 结构体构造器 / memblock 构造器）全部由 ACCEPT 变为 SyntaxError；合法带逗号形态不受影响；全量三层与 5 项门禁通过 |
| B-32 | `tc_parser_rhs.c` `tc_parse_struct_ctor_rhs`：删除「解析任意 RHS 后仅允许 memblock/struct 构造器」的分支，字段值不再接受任何调用型 RHS（嵌套构造器、funcall、cast、ptr_* 等）——附录 A `struct_constructor` 的字段值与 `const_struct_constructor` 的 `const_operand` 都只允许 operand（§6.1.2 调用型 RHS 不属于 operand）。既有 9 份依赖嵌套构造器的测试改写为合法等价形态（先 `let`/`var` 中间绑定再构造）：`struct_field_const_base_{nested,struct,memblock}`、`struct_field_static_let_base`、`phase5_struct_{nested,nested_assign,extract_indep,memblock,memblock_of_struct,memblock_deepcopy,ptr_nested_self_ref}`、`phase3_struct_nested`、`struct_field_operand_nested` 及 `test_type_check`/`test_struct_field_access` 内嵌源码；`struct_ctor_field_expr.tc` 断言改为新消息。新增 `struct_ctor_nested_constructor.tc` 与 `struct_ctor_memblock_constructor.tc`（VM check_fail + SyntaxError）；test-map 回填 1089 VM | 审计复现 `S(t: T(v: 1))` 与 `S(m: memblock(int32, count: 2, fill: 0))` 由接受且可运行变为 `SyntaxError`；`funcall` 的命名实参仍接受构造器（附录 A `named_argument`，未受影响）；改写后的等价合法程序输出与改写前一致；全量三层与 5 项门禁通过 |
| B-33 | `tc_parser_stmt.c` `tc_parse_field_assign_stmt`：RHS 为 `TC_TOK_FUNCALL` 时改走 `tc_parse_funcall_rhs`（与整绑定赋值一致）；`tc_struct_check_field_assign` 增 `TcAnalyzeCtx *ctx` 参数，RHS 为 `TC_RHS_FUNCALL_EXPR` 且 `ctx->func_env` 存在时改走 `tc_pass2_check_funcall_rhs`（否则 `tc_type_check_rhs` 不识别该 RHS 形态），pass2 调用点同步。新增 `tests/modules/FieldFuncallLib.tc` + `import_field_funcall.tc`（VM stdout+check_ok、AOT diff）；test-map 回填 1091 VM / 482 AOT | 审计复现 `s.v = funcall(Self.f)` 由 `SyntaxError: expected rhs expression` 变为接受（VM/AOT 均输出 7）；`s.v = 1` 与 `Self.x = funcall(...)` 行为不变；全量三层与 5 项门禁通过 |
| B-34 | `tc_parser_rhs.c` 三处 cast/bitcast 目标类型解析的 `allow_void` 由 1 改为 0（`tc_parse_cast_rhs`、`tc_parse_bitcast_rhs`、常量 cast 路径）：附录 A 的 `type` 产生式不含 `void`，`void` 只允许出现在函数返回类型，故 `cast(void, …)` / `bitcast(void, …)` 属语法拒绝（§1.3、附录 A.3 引言），不得降级为 SEM 的 TYPE_MISMATCH。新增 `cast_void_target.tc` / `bitcast_void_target.tc`（VM check_fail + SyntaxError）；test-map 回填 1093 VM | 审计两例由 `TypeMismatch: cast target must be scalar or ptr type` 变为 `SyntaxError: void type not allowed here`；`cast(int32, 1)` 等合法转换不受影响；`ptr<void>` 目标仍语法拒绝（与既有口径一致）；全量三层与 5 项门禁通过 |
| B-35 | `tc_parser.c` 新增 `tc_expect_comma_or_operand_count`（分隔处遇到 `)` → 个数不足）与 `tc_expect_rparen_or_operand_count`（收尾处遇到 `,` → 个数超出），均为 `TC_CE_OPERAND_COUNT`；缺逗号等形态错误仍为 `TC_CE_SYNTAX`。`tc_parser_rhs.c`（34 处逗号 + 23 处右括号）与 `tc_parser_stmt.c`（17 + 7 处）固定元数调用外壳统一改用新 helper，`tc_parse_read_stmt` 单独补两处。受影响期望更新：`diag_priority_syntax_before_name.tc`（VM/AOT）、`format_missing_operand.tc`、`test_parser` 的 bitcast 无效语法用例、`test_analyzer` 诊断优先级矩阵第 2 例。新增 7 份 `operand_count_*` 语料（add 缺参、writeln 缺参、read 多参、cast 缺参、ptr_load 缺参、memblock_load 缺参、neg 缺参；VM check_fail + OperandCountError）；test-map 回填 1100 VM | 审计 12 例：`add(int32, 1)`、`add(int32)`、`add(int32,1,2,3)`、`write(int32)`、`writeln(int32)`、`read(int32)`、`read(int32,a,b)`、`cast(int32)`、`ptr_load(int32)`、`ptr_size(int32)`、`memblock_load(int32, m)`、`neg(int32)` 由 `SyntaxError: unexpected token` 全部变为 `OperandCountError`；`writeln(int32, a, a)` 的多余操作数行为不变；合法调用与全量三层、5 项门禁通过 |
| B-36 | 无需新代码：`%--d` 的重复标志由 B-3 已在分析器报 `TC_CE_FORMAT_SPECIFIER`（`format_duplicate_flag.tc` 语料）；32 字符宽度上限由 B-27 的 64 字节缓冲 + 宽度范围检查消除（30/31 位宽度现均报 `FormatSpecifierError: format width or precision out of range`，与附录 A「`format_width` 无长度上限」一致）。本条为审计对 B-3/B-27 的补充维度，记入台账以备追溯 | 复核：`write(int32, %--d, 1)` → `FormatSpecifierError: duplicate format flag`；`%`+30 个数字+`d` 与 `%`+31 个数字+`d` → 同为 `FormatSpecifierError: format width or precision out of range`（不再是 `SyntaxError: format specifier too long`）；全量三层与 5 项门禁通过 |
| B-38 | `tc_parser.c` `tc_parse_module_body`：`#lib` 模式下顶层 VAR/LET 行在语法阶段直接报 `TC_CE_SYNTAX: non-static value declaration is not allowed in #lib`（附录 A `library_module` 只接受带可见性的 static 成员），不再放行到分析器的 `MODULE_LAYER`（SEM，阶段错位）。分析器中的旧检查保留为防御。新增 `tests/errors/static/lib_toplevel_var_syntax.tc`（VM check_fail + SyntaxError 与 fail_msg）；test-map 回填 1102 VM | 审计复现 `#lib` + `var x: int32 = 1` 由 `ModuleLayerError`（SEM）变为 `SyntaxError`（第 3 阶段）；`#lib` + `let` 同；合法 `public static var` 不受影响；全量三层与 5 项门禁通过 |
| B-39 | `tc_parser.c` `tc_parse_source_to_program`：#program 模式在解析主体前按**源序**扫描全部 Token 行，出现 `Self` 即报 `TC_CE_PROGRAM_MODE_MISUSE`（SYN，第 3 阶段）——原实现只抓行首 `Self`，其余由分析器在 SEM 检查，导致同文件后面更晚的语法错误抢先（§11 第 1 条）。`test_module` 的 Self 用例改为断言解析阶段即拒绝。新增 `tests/errors/static/self_before_later_syntax.tc`（VM check_fail + ProgramModeMisuseError）；test-map 回填 1103 VM | 审计复现（第 2 行 `Self.x` + 第 3 行 `add(int32, 1)` 缺参）现报第 2 行 `ProgramModeMisuseError: Self is not allowed in #program`（原报第 3 行 SyntaxError）；嵌套块内的 `Self` 同样在源序位置报出；`#lib` 的 `Self` 用法不受影响；全量三层与 5 项门禁通过 |
| B-43 / B-58 | `tc_memblock_exec.c` 与 `tc_aot_rt.c` 的 `memblock_copy` 区间判定：`length == 0` 时不再短路——§6.7.2.4 只放宽「下标**等于** count」，下标**大于** count 恒非法，故空拷贝的 dst/src 越界下标同样报 `TC_RE_MEMBLOCK_INDEX_OUT_OF_RANGE`。新增 `tests/errors/runtime/memblock_copy_empty_{src,dst}_oob.tc`（VM fail_msg、AOT runtime_fail）；test-map 回填 1105 VM | 审计两例（`memblock_copy(int32, b, 0, a, s, n)` 与 `memblock_copy(int32, b, 9, a, 0, n)`，`s=9`、`n=0`、`count: 4`）由正常结束变为 `MemblockIndexOutOfRange`（VM/AOT 一致）；合法空拷贝（下标 == count）仍接受；全量三层与 5 项门禁通过 |
| B-58 覆盖补强 | 澄清测试分层：常量 dst 下标（`9`）在 §6.7.2.4 下属**静态**拒绝，原 `tests/errors/runtime/memblock_copy_empty_dst_oob.tc` 用字面量 9 注册为 `run_runtime_fail`，因此 B-58 的**运行期** dst 侧区间合取分支实际无覆盖（AOT 侧因静态失败从未生成/运行宿主可执行文件）。改为：该语料 dst 下标换成变量（`var d: usize = 9u`）以真正走运行期分支；常量路径另立 `tests/errors/static/memblock_copy_constant_dst_index.tc`（`memblock_copy(int32, b, 9, a, 0, 0)`）并在 VM/AOT 两侧以 `check_fail` 断言 `MemblockIndexOutOfRange`；`scripts/aot/run_tests.sh` 的 `run_runtime_fail` 新增「确已运行则报告的退出码须与实际一致」断言即在该语料上生效。test-map 回填 1125 VM / 496 AOT | 变量 dst 下标语料：`tc-vm -c`/`tc-aot -c` 均 rc=0（静态通过）→ 运行期两后端均报 `memblock index out of range`；AOT `run failed (exit 1)` 被新断言校验；常量语料：VM/AOT `--check` 均报 `MemblockIndexOutOfRange`；全量三层与 5 项门禁通过 |
| B-44 / B-59 / B-60 | 无需新代码：常量负 `length`（B-44）与常量负 `dst` 下标（B-59）已由 B-21 的 `tc_memblock_check_copy` 常量区间判定覆盖；35 位宽度的越界诊断（B-60）已由 B-27 的 64 字节缓冲 + 宽度范围检查覆盖。三例复核结果见「验证」列 | `memblock_copy(int32, b, 0, a, 0, -1)` 与 `memblock_copy(int32, b, -1, a, 0, 1)` 均为静态 `MemblockIndexOutOfRange`（原为运行期才报）；`%`+35 个 `9`+`d` 报 `FormatSpecifierError: format width or precision out of range`（原为 `SyntaxError: format specifier too long`） |
| B-42 | `TcExecuteCtx` 增 `slot_capacity`（独立 VM 取声明槽位数、嵌入 VM 取含临时区的容量），`tc_ptr_exec.c` 的 load/store/arith 与 `tc_memblock_exec.c` 的 memcopy_unsafe 在解码槽索引后追加 `slot >= slot_capacity` 判定，越界按 `TC_RE_NULL_POINTER_DEREFERENCE` 报（与 B-10 的非法编码口径一致，§1.3 零 UB）。新增 `ptr_forged_slot_oob_{load,store,memcopy}.tc`（VM fail_msg；AOT 侧运行时尚无上界校验，其确定性归 B-57）；test-map 回填 1108 VM | 审计复现 `bitcast(ptr<int32>, 0x7FFFFFFD)` 后 `ptr_load` 由 SIGSEGV（ASan heap-buffer-overflow）变为 `NullPointerDereference`；`ptr_store` 与 `memcopy_unsafe` 同样不再越界读写；合法指针（含嵌入临时槽位）不受影响；全量三层与 5 项门禁通过。注：AOT 运行时仍无槽位上界（属 B-57 范围） |
| B-45 | `tc_sem_bitwise.c` `tc_exec_shl`：把 `val_bits == 0` 的早退移到 `k >= n` 溢出判定**之后**——§6.4.2/§6.4.2.1 规定 strict `shl` 的溢出判定与被移位数的值无关。`tests/unit/runtime/test_shift.c` 原「val=0 恒为 0」断言改为「k ≥ n 报 TC_RE_INTEGER_OVERFLOW，k < n 仍为 0」。新增 `tests/errors/runtime/shl_zero_shift_overflow.tc`（VM fail_msg、AOT runtime_fail）；test-map 回填 1109 VM | 审计两例：`shl(int32, z, k)`（z=0、k=32）由静默 `0` 变为 `IntegerOverflow: shift left overflow`；`let r: int32 = shl(int32, 0, 32)` 报 `ConstantOverflow`；wrap 模式仍为 0；k < n 的零值移位仍为 0；全量三层与 5 项门禁通过 |
| B-46 | `tc_io.c` `tc_fp_round`：把 `keep <= 0` 拆开——`keep < 0`（需保留的有效位完全落在首位有效数字之前）恒舍入为零，仅 `keep == 0`（舍入位恰为首位）才按该位与后续位做 roundTiesToEven 判定。新增 `tests/valid/format_float_small_rounding.tc`（0.00009/2^-40/6e-8/9e-7 四例；VM stdout+check_ok、AOT diff）；test-map 回填 1111 VM / 483 AOT | 审计三例：`%.3f` of 0.00009 由 `0.001` 变为 `0.000`；`%.0f` of 2^-40 由 `1` 变为 `0`；`%.6f` of 6e-8 由 `0.000001` 变为 `0.000000`；边界对照 `%.6f` of 9e-7 仍正确进位为 `0.000001`（keep == 0 路径不变）；VM/AOT 输出一致；全量三层与 5 项门禁通过 |
| B-47 / B-48 | `tc_lexer.c` 浮点字面量：①float32 后缀改用 `strtof` **直接**按 roundTiesToEven 舍入到 binary32（原为 strtod 的 double 再截断，二次舍入，§2.4.1）；②范围判据改为「**舍入后**为 ±∞ 或非零有限值舍入为零」——删除过严的 `fabs(value) > FLT_MAX` 与 2^-150 阈值，`3.4028235e38f`（舍入到 FLT_MAX）与可表示非规格化数均合法。新增 `tests/valid/float32_literal_direct_rounding.tc`（1.0000001788139343f → 0x3F800001；3.4028235e38f → 0x7F7FFFFF；VM stdout+check_ok、AOT diff）；test-map 回填 1113 VM / 484 AOT | B-47 审计例：`var x: float32 = 1.0000001788139343f` 的 `bitcast(uint32, x)` 由 `3f800002` 变为 `3f800001`，与 `read(float32)` 的 strtof 口径一致；B-48：`3.4028235e38f` 由拒绝变为接受（输出 `2139095039` = `0x7F7FFFFF`）；`1e-46f`（舍入为零）仍拒绝、`1e-45f` 仍接受；VM/AOT 输出一致；全量三层与 5 项门禁通过 |
| B-49 | `tc_ptr_exec.c`：`tc_ptr_read_offset` 改以 `uint64_t`（usize 位模式）返回偏移，`tc_exec_ptr_arith` 用无符号运算计算新槽索引（消除 ≥ 2^63 偏移转 int64_t 的有符号溢出 UB），并在结果 `>= slot_capacity` 时报 `TC_RE_NULL_POINTER_ARITHMETIC`（与 B-10/B-42 的非法指针值口径一致，避免回绕/截断成可能指向合法槽位的编码）。新增 `tests/errors/runtime/ptr_arith_huge_offset.tc`（VM fail_msg）；test-map 回填 1114 VM | 审计复现（`ptr_sub(int32, p, 9223372036854775808u)`）由 UBSan `signed integer overflow` / 垃圾指针变为确定的 `NullPointerArithmetic`；容量内的 `ptr_add(int32, p, 1u)` 仍正常（输出 2）；全量三层与 5 项门禁通过。注：AOT 运行时的同源有符号运算归 B-57 一并处理 |
| B-50 | `tc_aot_emit_func.c`：非嵌入模式的 TC 函数与前置声明去掉 `static`（改外部链接）——`#lib`-only 程序的函数可能无调用点，`static` + `-Werror` 会触发 `-Wunused-function` 使 `-r` 失败；生成代码总是单文件编译（无需 static 隔离符号）。`scripts/aot/run_tests.sh` 增 `run_diff_test MathLib.tc`（#lib-only 的 `-r` 覆盖，此前 220 个 diff 测试无一是 #lib-only）；test-map 回填 485 AOT | 审计复现：`tc-aot -r tests/valid/MathLib.tc` 由 `error: unused function 'tc_aot_func_0' [-Werror,-Wunused-function]`（rc=1）变为 rc=0；抽测多个 `#lib`-only 语料均通过；嵌入模式仍由函数表引用、行为不变；全量三层与 5 项门禁通过 |
| B-51 | `TcRhs.u.ptr_address` 增 `TcResolvedBinding binding`；`tc_ptr_check.c` 的 `TC_RHS_PTR_ADDRESS` 分支在解析成功后固化目标绑定；`tc_aot_emit_rhs.c` 优先用该绑定槽位（按名解析保留为兜底）——形参在代码生成期无法按名解析，此前 `ptr_address(int32, <形参>)` 令 `tc-aot -o` 报 `code generation failed`。`scripts/aot/run_tests.sh` 为既有 `ptr_address_param_load.tc` 增加 `run_diff_test`（该语料此前仅 `--check` 覆盖，故未暴露）；test-map 回填 486 AOT | 审计复现：`tc-aot -o x.c` 对 `public func f(x: int32) ptr<int32> then var p: ptr<int32> = ptr_address(int32, x) ...` 由 code generation failed 变为 rc=0；`tc-vm --check` / `tc-aot --check` 仍 rc=0；`ptr_address_param_load.tc` 的 diff 测试通过（VM/AOT 一致）；全量三层与 5 项门禁通过 |
| B-52 | `tc_sem_fp.c`：strict 浮点结果判定不再使用宿主 `FE_UNDERFLOW`（部分平台为 tininess-before-rounding），统一改用位级 `tc_fp_no_fenv_underflow`（结果指数域全 0 且精确数学值 ≠ 舍入结果）；该 helper 及其 64 位整数对运算的实现 guard 由 `#ifndef TC_HAVE_FENV` 改为 `#if 1`，fenv 与非 fenv 构建共用同一判据（§6.3.2 要求不随宿主浮点环境变化）。新增 `tests/valid/fp_tininess_after_rounding.tc`（VM stdout+check_ok、AOT diff）；test-map 回填 1116 VM / 487 AOT | 审计例：`mul(float32, 2^-126, 1-2^-24)`（精确积 = 2^-126 − 2^-150，舍入到最小正规数）由 VM/AOT 均报 `FloatUnderflow` 变为两后端均输出 `8388608`（0x00800000）且 rc=0，与不带 `TC_HAVE_FENV` 的构建一致；真实下溢（`1e-30f * 1e-20f`）仍报 `FloatUnderflow`；全量三层与 5 项门禁通过 |
| B-53 | `tc_io.c` `tc_io_write_formatted`：整数/布尔格式化的 `digits` 由固定 `char digits[80]` 改为「内建 80 字节 + 精度超出时按 `precision + 1` 堆分配」的缓冲，容量随精度增长；所有 `tc_io_prec_pad`/`tc_io_u64_to_base` 调用改用实际容量，新增 `done:` 统一释放路径（含各早退分支），使 §10.4 允许的精度 0～65535 不再被内建缓冲上限降级为 `TC_RE_IO`。新增 `tests/valid/format_precision_over_buffer.tc`（`%.80d`/`%.200d`/`%.1000d`/`%.79d`，VM stdout + AOT diff）与 `tests/valid/format_precision_max.tc`（`%.65535d` 仅 `--check`，对齐既有 `format_width_max`，避免 6 万余字符黄金输出）；test-map 回填 1118 VM / 489 AOT | 审计复现：`writeln(int32, %.80d, a)` 由 `error [IOError]: output failed`（rc=1）变为正常输出 79 个 `0` 后跟 `7`（rc=0）；`.200d`/`.1000d`（含负号）同样正常且 VM 与 AOT 输出逐字节一致；边界对照 `%.79d`（内建缓冲内）输出不变；`%.65535d` 两后端 `--check` 均 rc=0，超过 65535 仍由词法/静态阶段拒绝（B-27/B-36 口径不变）；全量三层与 5 项门禁通过 |
| B-54 | 无需新增 src 改动：B-56（`tc_analyzer.c` 依赖 Pass2 循环为每个 dep 现场构建成员索引并切换 `func_env.members`/`module_index`）已同时修复「裸名撞本库成员 → `TC_CE_FUNCTION_SCOPE_ACCESS` 按**当前被分析模块**判定」。本轮补足依赖侧回归覆盖：新增 `tests/modules/bare_scope_neg/` 六组「`#lib` ＋ `#program` import」对照语料，覆盖 §8.4.1 列举的全部裸名形态（RHS 读 / 赋值目标 / 输出操作数 / `return` / 条件 / `let` 初始化器），VM 用 `run_expect_check_fail` 断言行文本与错误码名，AOT 用 `run_check_fail` 断言同一诊断（库内诊断定位仍指向库自身文件）；test-map 回填 1124 VM / 495 AOT | 审计复现（同一 `#lib` 源文本作入口 vs 被 `#program` 导入）六形态实测：两路径均报 `FunctionScopeAccessError` 且消息一致（`function scope access: use Self.C`），此前依赖路径报 `UndefinedFunction`/`UndefinedVariable`；入口侧既有 `tests/errors/static/self_bare_*.tc` 断言不变；全量三层与 5 项门禁通过 |
| B-55 | ①`tc_embed.c` `tc_embed_slot_write` 成功路径同时清 `error_flag` 与 `error_message`（此前只清 flag，`tc_embed_get_error` 返回上次失败的旧消息）；`tc_embed.h` 与 `TC-Embed详细设计说明书-0.0.44.md` §10.2 同步该契约（`tc_embed_slot_read` 为只读接口不改错误状态）。②`src/aot/main.c` `tc_aot_run_generated` 的 POSIX 分支用 `WIFEXITED`/`WEXITSTATUS`（信号 → 128+`WTERMSIG`）拆出真实退出码，不再把 `system()` 的 wait status 当作退出码打印；`scripts/aot/run_tests.sh` 的 `run_runtime_fail` 增加断言：只要生成的宿主可执行文件确已运行，stderr 的 “run failed (exit N)” 必须等于实际子进程退出码（覆盖全部运行时失败语料，防止回到 256）。③`src/libtc/tc_lib.c` `tc_compile_file_opts` 删除死代码 `TcCompileOptions empty_opts`（`memset` 后从未使用，`search` 初值已是 NULL）。④`src/aot/tc_aot_rt.{c,h}` 的 `tc_aot_ptr_load` 增 `TcTypeTag load_type` 形参，pointee 为 `bool` 时按 §3.4/§6.8.2 规范化到 {0,1}（与 `tc_ptr_exec.c` 对称）；`tc_aot_emit_rhs.c` 发射调用时传入 `pointee_type.tag`。新增 unit：`test_embed_slot_out_of_range` 增“成功写清消息”断言、`test_embed_aot.c` 增 `test_aot_ptr_load_bool_type`（直接调用运行时的 6 条断言） | ①unit `test-embed`：越界写后消息非空 → 成功写后 `had_error()==0` 且消息为空；②`tc-aot -r` 对 `div_zero.tc`/`memblock_oob_rt.tc` 的 stderr 由 `run failed (exit 256)` 变为 `run failed (exit 1)`；AOT 全量 554 项通过（含新断言）；③构建无 `-Wunused` 类告警、`--filter` 与全量行为不变；④unit `test-embed-aot`：槽位 2 → bool 读回 1、槽位 0 → 0、`TC_INT32` 读回保持 2，生成 C 中出现 `tc_aot_ptr_load(slots, …, TC_BOOL, …)`；全量三层与 5 项门禁通过 |
| B-57 | AOT 运行时的指针槽编码容量上界（与 VM 的 `ctx.slot_capacity` 口径对齐）：`tc_aot_codegen.c` 在生成 C 中发 `TC_AOT_SLOT_CAPACITY`（嵌入模式与生成头文件同值，`#ifndef` 保护；无槽位程序也有定值）；`tc_aot_ptr_load`/`tc_aot_ptr_store`/`tc_aot_memcopy_unsafe` 增 `size_t slot_capacity` 形参，解码出的槽号 ≥ 容量即按空指针解引用报 `TC_RE_NULL_POINTER_DEREFERENCE`；`tc_aot_ptr_arith` 增容量形参并把偏移改为 `uint64_t`（不再缩窄 `int64_t`），用无符号运算复刻 `tc_exec_ptr_arith`——解码槽号越界或结果 ≥ 容量均报 `TC_RE_NULL_POINTER_ARITHMETIC`（一并覆盖 B-49 遗留的 AOT 同源有符号运算）。发射端（`tc_aot_emit_rhs.c` / `tc_aot_emit_stmt.c`）随调用传容量；unit 增 `test_aot_ptr_slot_capacity`（load/store/arith 越界同码拒绝 + 容量内成功 + 2^63 偏移不触发有符号溢出）；AOT `run_runtime_fail` 增 4 条既有语料注册（`ptr_forged_slot_oob_{load,store,memcopy}`、`ptr_arith_huge_offset`） | 审计复现（`var p: ptr<int32> = bitcast(ptr<int32>, 0x7FFFFFFD)` + `ptr_load`）：`tc-aot -r` 连续 5 次由「rc=0 且 5 个互不相同的垃圾值（63684056/1208226900/1852140901/1197434691/1634030188）」变为 5 次均 `null pointer dereference` + `run failed (exit 1)`，与 VM 的 `NullPointerDereference` 同码同行；`ptr_forged_slot_oob_{load,store,memcopy}` 与 `ptr_arith_huge_offset` 两后端 stderr 首个诊断逐字一致（`run_runtime_fail` 断言）；容量内 `ptr_add/ptr_load` 行为不变；全量三层与 5 项门禁通过 |
| B-64 | `tc_struct_check.c` 新增 `tc_split_qualified_member`：限定名 `<?>.<成员名>` 按**最后一个点**切分（模块名允许含内部点），`tc_struct_lookup_written` 与 `tc_struct_table_find` 均改用它——此前两处都要求「恰好一个点」才走限定名分支，而 `a.b.c.tc` 的模块名就是 `a.b.c`，本地结构体被规范化成 `a.b.c.A` 后落回裸名分支，查不到 → `TC_CE_UNDEFINED_STRUCT`。新增 `tests/valid/struct_dotted.v1.tc`（模块名 `struct_dotted.v1`：本地 `struct A` 的构造器 / 字段读 / `ptr<A>` 取址与解引用；VM stdout+check_ok、AOT diff+check_ok）；test-map 回填 1127 VM / 498 AOT | 审计复现：同一源文本 `abc.tc` 输出 `7`、改名 `a.b.c.tc` 报 `undefined struct 'a.b.c.A'`；修复后 `a.b.c.tc` 两后端均输出 `7`（rc=0），`abc.tc` 行为不变；新语料两后端均输出 `7/9/16` 且 diff 一致；`import <模块>.<结构体>`（单点）路径与 private 判定不变（既有 `imported_struct_*` / `import_struct_type` 全通过）；全量三层与 5 项门禁通过 |
| B-65 | 根因：符号表全模块共享（slot 全局唯一）而 `stmt_index` 每模块各自从 0 编号，「名字 + def_stmt_index」解析会命中另一模块的同名绑定。① `TcSymbol` 增 `module_name`（借用 `TcProgram.module_name`），由 `tc_pass1_collect_symbols` 按模块回填；② `tc_find_symbol_by_def_index` 增 `module_name` 形参并优先本模块匹配（`tc_visible_add_from_global` 同步带上模块，可见表沿用同一优先级）——修复入口变量被解析到库内同名局部导致**读值串槽**；③ CFG（`tc_cfg.c`）写槽改用 Pass1 固化的 `var_def.binding`（与读侧绑定同源），`read` 语句改用 `io_read.binding`，`tc_cfg_find_def`/`tc_cfg_find_visible` 增模块优先过滤（`TcCfgBuildCtx.module_name`，由 `tc_cfg_build`/`tc_cfg_build_items` 传入）；④ 新增 `tests/modules/SameNameLib.tc` + `import_same_name_shadow.tc`（库内嵌套块局部 `b` vs 入口 `b`；VM stdout+check_ok、AOT diff+check_ok）；⑤ 修正 `diamond_import_{ok,swapped_ok}.tc` 的 VM 期望：两条臂局部同名 `s` 串槽曾被写成 `3/3`、`4/4`，正确值为 `3/4`、`4/3`。test-map 回填 1129 VM / 500 AOT | 审计复现（库内 `funcall` 目标含同名局部 `var b`）：修复前三处证据——(a) `tc_cfg_find_def` 写槽取到库内槽位而绑定为入口槽位（假阳性 `UninitializedVariable`）；(b) 入口 `writeln(bool, b)` 实际读到库内同名局部（本库返回 `false` 却输出 `true`，属静默串槽）；(c) 菱形语料两条臂互相串槽。修复后：审计复现与新增语料均输出 `false` 且 rc=0（VM/AOT 一致），菱形输出恢复为各臂自己的值（`3/4`、`4/3`）；`self_member_undefined` / `self_access_ok` / `static_let_rule_ok` 等 `Self.` 与跨块用例全部保持既有诊断；全量三层与 5 项门禁通过 |
| B-62 | 无需新增 src 改动：B-30/B-31（附录 A 列表产生式统一拒绝尾随逗号「trailing comma not allowed in list」并要求逗号分隔「expected , or )」）已覆盖 const 变体。本轮补足 const 侧回归覆盖：新增 4 条语料——模块级 `public static let` 的 `const_struct_constructor` 尾随逗号、函数内 `let` 的该产生式缺失逗号、模块级 `static let` 的 `const_memblock_elems_ctor` 尾随逗号、函数内 `let` 的该产生式缺失逗号；VM `run_expect_check_fail`（消息+`SyntaxError`）与 AOT `run_check_fail` 双端断言。test-map 回填 1133 VM / 504 AOT | 审计四例（`S(a: 1, b: 2,)`、`S(a: 1 b: 2)`、`memblock(int32, count: 2, 1, 2,)`、`…, 1 2)`）实测全部由 rc=0 变为 `SyntaxError`（rc=1），且 const 与非 const 路径错码一致；对照 `memblock(int32, count: 2, fill: 0,)` 两路径同样拒绝（既有行为）；全量三层与 5 项门禁通过 |
| B-37 | `tc_struct_check.c` 新增 `tc_field_split_variable_base`：字段读（`tc_struct_check_field_access`）与字段赋值（`tc_struct_check_field_assign`）在解析基址前按**名称解析**纠正解析器的分类——若基址第一个点之前的部分能按 `tc_find_named_binding` 命中可见绑定，则说明这是变量基址（`A.i.v`），把点后部分并回字段链首位；否则保持 `<模块名>.<成员>.<字段>` 的限定名分类（如 `StructFieldSelfLib.root.v` 不受影响）。新增 `tests/valid/uppercase_var_field_read.tc`（`A.i.v` 与大写/小写对照）与 `uppercase_var_field_assign.tc`（`A.i.v = 5`），VM stdout+check_ok、AOT diff+check_ok；test-map 回填 1137 VM / 508 AOT | 审计复现：`var A: Outer = Outer(i: inner)` 后 `writeln(int32, A.i.v)` 由 `UndefinedVariable: undefined variable 'i'` 变为输出 `1`（与换成小写 `a` 的行为一致）；`A.i.v = 5` 同样由失败变为输出 `5`；限定名基址用例（`struct_field_operand_self_base`、`struct_field_operand_*`、`imported_struct_*`、`qualified_*`）全部保持既有结果；全量三层与 5 项门禁通过 |

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
| 35 | 阶段 2-§2.6.2 严格同型比较 | `f1ecca3 fix(0.0.44-2.6.2): compare full operand types, not just tags` |
| 36 | 阶段 2-B22 字面量类型专用码优先 | `3807ee2 fix(0.0.44-B22): report LITERAL_TYPE in every literal position` |
| 37 | 阶段 2-B23 条件位置保留具体码 | `f073c9d fix(0.0.44-B23): keep specific codes inside conditions` |
| 38 | 阶段 2-B24 常量 cast 字面量错码 | `c05f4b1 fix(0.0.44-B24): report literal codes for constant cast literals` |
| 39 | 阶段 2-B25 goto 标签判定次序 | `af362e4 fix(0.0.44-B25): prefer jumping into a child block over sibling mismatch` |
| 40 | 阶段 2-B26 非规格化浮点字面量 | `39ab503 fix(0.0.44-B26): accept representable denormal float literals` |
| 41 | 阶段 2-B27 格式说明符长度上限 | `febd46f fix(0.0.44-B27): stop rejecting long legal format specs` |
| 42 | 阶段 2-B28 类型递归深度上限 | `6ab3088 fix(0.0.44-B28): cap type-expression recursion depth` |
| 43 | 阶段 2-B29 数字分隔符规则 | `c6039cb fix(0.0.44-B29): enforce digit-separator placement rules` |
| 44 | 阶段 2-B30/B31 列表产生式逗号规则 | `2b93ace fix(0.0.44-B30/B31): enforce comma rules in list productions` |
| 45 | 阶段 2-B32 结构体构造器字段值限 operand | `3ac1492 fix(0.0.44-B32): restrict struct-constructor field values to operands` |
| 46 | 阶段 2-B33 字段赋值 RHS 支持 funcall | `656cc42 fix(0.0.44-B33): accept funcall on the right of a field assignment` |
| 47 | 阶段 2-B34 cast/bitcast 目标不得为 void | `41c7cd8 fix(0.0.44-B34): reject void as a cast/bitcast target in the parser` |
| 48 | 阶段 2-B35 操作数个数专用码 | `cfb2596 fix(0.0.44-B35): report OPERAND_COUNT for arity mismatches` |
| 49 | 阶段 2-B36 格式说明符重复标志/长度 | 台账记录（由 `5f1dd2a` B-3 与 `febd46f` B-27 闭合，无新代码） |
| 50 | 阶段 2-B38 `#lib` 顶层裸 var/let 语法拒绝 | `d434245 fix(0.0.44-B38): reject bare top-level var/let in #lib at parse time` |
| 51 | 阶段 2-B39 `#program` Self 按源序在 SYN 报出 | `a47d076 fix(0.0.44-B39): report Self in #program at parse time in source order` |
| 52 | 阶段 2-B43/B58 空拷贝区间检查；B44/B59/B60 复核闭合 | `61098f5 fix(0.0.44-B43/B58): check empty memblock_copy ranges at runtime` |
| 53 | 阶段 2-B42 伪造槽索引上界校验 | `5288e41 fix(0.0.44-B42): bound-check decoded pointer slot indices` |
| 54 | 阶段 2-B45 strict shl 零被移位数溢出 | `c60ea01 fix(0.0.44-B45): detect strict shl overflow regardless of value` |
| 55 | 阶段 2-B46 `%f` 极小量舍入 | `7601a4e fix(0.0.44-B46): round tiny values to zero when no significant digit is kept` |
| 56 | 阶段 2-B47/B48 float32 字面量直接舍入与边界判据 | `faea694 fix(0.0.44-B47/B48): round float32 literals directly and accept rounded boundary values` |
| 57 | 阶段 2-B49 ptr 算术 usize 偏移无符号语义 | `90ede78 fix(0.0.44-B49): use unsigned semantics for pointer arithmetic offsets` |
| 58 | 阶段 2-B50 AOT `-r` 支持 #lib-only | `f0447fb fix(0.0.44-B50): link AOT functions externally for non-embed builds` |
| 59 | 阶段 2-B51 形参取址的 AOT 代码生成 | `7a6a94a fix(0.0.44-B51): use the resolved binding for ptr_address in AOT` |
| 60 | 阶段 2-B52 strict 下溢判据与宿主无关 | `50d3bae fix(0.0.44-B52): judge float underflow by rounded bits, not host FE_UNDERFLOW` |
| 61 | 阶段 2-B53 整数格式化精度上限 | `69d81da fix(0.0.44-B53): size the integer format buffer by precision` |
| 62 | 阶段 2-B54 依赖模块裸名引用错码一致性 | `ac8d01d test(0.0.44-B54): cover bare member access inside imported libraries` |
| 63 | 阶段 2-B55 embed/工具链四项细节 | `f694d25 fix(0.0.44-B55): embed error state, run exit status, dead code, AOT bool load` |
| 64 | 阶段 2-B58 运行期 dst 侧分支覆盖补强 | `b88d249 test(0.0.44-B58): exercise the runtime dst-side empty-copy bound` |
| 65 | 阶段 2-B57 AOT 伪造指针容量上界 | `056fff6 fix(0.0.44-B57): bound AOT pointer slot indices by capacity` |
| 66 | 阶段 2-B64 含点文件名的本地结构体解析 | `cf0de99 fix(0.0.44-B64): split qualified struct names at the last dot` |
| 67 | 阶段 2-B65 跨模块同名符号解析 | `9ab5cbc fix(0.0.44-B65): resolve same-named symbols per module` |
| 68 | 阶段 2-B62 const 列表产生式回归覆盖 | `9569331 test(0.0.44-B62): cover the const list productions` |
| 69 | 阶段 2-B37 大写变量嵌套字段解析 | 本提交 `fix(0.0.44-B37): classify field-access bases by name resolution` |

---

*— 台账结束 —*

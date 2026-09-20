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
| B-5 | `memblock` `N`/`count:` 来源校验过宽＋错码 | ☐ | |
| B-6 | 实参个数检查遮蔽重复/未知/顺序 | ☐ | |
| B-7 | VM 按槽位标签渲染 `write` | ☐ | |
| B-8 | 结构体字段指针操作数：VM 内部错误 / AOT 空指针 | ☐ | |
| B-9 | 顶层 `let` 作 `memblock_copy` 下标：仅 VM 失败 | ☐ | |
| B-10 | `bitcast(ptr<T>, usize)` 解引用两后端分歧 | ☐ | |
| B-11 | 指针别名清零后读字段：VM 内部错误 | ☐ | |
| B-12 | `#program` 结构体名 vs `import` 名冲突被放过 | ☐ | |
| B-13 | libtc 内存入口不解析 `import` | ☐ | |
| B-14 | 依赖模块诊断定位到入口文件 | ☐ | |
| B-15 | `tc-aot -r` 相对路径失败 | ☐ | |
| B-16 | 模块文件 I/O 失败映射为语言码 | ☐ | |
| B-17 | `#lib static var` 缺初始化器错码 | ☐ | |
| B-18 | `implementation error` 泄漏 | ☐ | |
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
| 2.6.1 | 确定初始化 DFA 读集不完整（7 个场景） | ☐ |
| 2.6.2 | 操作数/字段类型只比较 `tag`（6 类） | ☐ |
| 2.6.3 | 名称冲突与作用域漏检（5 类） | ☐ |
| 2.6.4 | 控制流/可达性漏检（`while true` 后不可达；`else if`） | ☐ |
| 2.6.5 | `#lib` 顶层可执行语句未拒绝 | ☐ |
| 2.6.6 | `isize` 被当作 `usize` | ☐ |

### 阶段 2 逐条记录

| 条目 | 改动 | 验证 |
| ---- | ---- | ---- |
| B-1 | `tc_func_check.c`：`position == 1 && is_void`（void 作值）由 `TC_CE_FUNCALL_POSITION` 改为 `TC_CE_FUNCALL_RESULT_TYPE`；非 void 返回类型与接收类型不符由 `TC_CE_FUNCALL_RESULT_TYPE` 改为 `TC_CE_TYPE_MISMATCH`（[语言标准 §8.2.3]、附录 B.4/B.12）。`funcall_result_type.tc` 注释更正；`run_expect_check_fail` 增可选错误码名断言（不新增注册行） | `tc-vm -e -c` 三例实测；`--filter funcall` 20/20；全量三层通过 |
| B-2 | `tc_analyzer.c` 新增 `tc_sem_salvage`/`tc_sem_diag_earlier`：Pass2（6a–8）失败后仍以**独立临时诊断**尝试阶段 12 调用图与阶段 11 CFG/确定初始化；阶段 11 失败后再补阶段 12。仅当后阶段诊断 (行,列) 更靠前时替换（[语言标准 §11] 第 2 条；依编译器标准 §1.3 第 4–8/11/12 阶段同属 SEM）。CFG 读集按槽位展开、调用图只需签名，故 salvage 安全。新增语料 `diag_priority_{recursion,unreachable}_before_name.tc`（VM 断言消息＋打印名，AOT 断言消息）；`test-map.md` 规模回填 1004 VM / 466 AOT | 两复现实测（Recursion 第 3 行、Unreachable 第 4 行胜出）；`--filter diag_priority` 21/21；全量三层通过 |
| B-3 | 重复格式标志：`tc_types.h` 的 `TcFormatFullSpec` 增 `flag_repeat`；`tc_types.c` 的 `tc_format_spec_parse` 对重复 `-`/`+`/`#` 与非连续第二段 `0` **不再返回 0**（附录 A `{ format_flag }` 形态合法），只置 `flag_repeat`；`tc_analyze_6e.c` 在 SEM 阶段报 `TC_CE_FORMAT_SPECIFIER`（duplicate format flag）。附带修 AOT 代码生成：`(TcFormatFullSpec){...}` 改**指定初始化器**，避免新增字段触发 `-Wmissing-field-initializers`。新增语料 `format_duplicate_flag.tc`（VM 断言消息＋打印名，AOT 断言消息）与 4 条单元断言；test-map 回填 1005 VM / 467 AOT | `%--d`/`%++d`/`%##x`/`%0-0d` → FormatSpecifierError；`%-+d`/`%08d` 接受；`%8-d` 仍 SYNTAX；`--filter format` 47/47；全量三层通过 |
| B-4 | `tc_analyzer_pass2.c` 的 `let`（ConstDef）路径在 `tc_type_check_rhs` **之前**增加 §5.2.1 第 3 步预检：RHS 为裸标识符且绑定是 `var`/形参/`static var` → 立即 `TC_CE_CONSTANT_EXPRESSION`，不比较结果类型（`Self.`/限定名仍走 6d 常量来源规则）。新增语料 `let_var_type_mismatch.tc`（声明类型与 var 不同，锁定次序）；test-map 回填 1006 VM / 468 AOT | `let a: int8 = v`（v: int32）→ ConstantExpressionError（原 TypeMismatch）；同类型 var、形参、`var` 初始化/赋值对照均正确；全量三层通过 |

---

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
| 11 | 阶段 2-B4 `let` RHS 常量性先于类型 | 本提交 `fix(0.0.44-B4): check let RHS constness before type comparison` |

---

*— 台账结束 —*

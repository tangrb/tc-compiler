# TC 开发调试工具链 0.0.47

## 0. 依据与范围

- 语言语义以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为唯一权威。本分支
  不修改语言标准：51 个 opcode、80 个诊断码、零 pass 优化基线、ABI 与平台档案全部冻结。
- 本文件规定 0.0.47 分支的工具链范围、接口契约、里程碑与验收。工具链能力属于编译器实现，
  不构成语言可观察行为；任何工具输出都不得改变程序的标准语义。
- RTOS 能力（单线程平台扩充、中断语义、并发与调度）不在本分支范围，移交 0.0.48（§9）。
- 交付入口仍只有 `tc-vm` 与 `tc-aot`；不新增公共 C 接口、嵌入 API 或调试器 SDK。

## 1. 目标与约束

0.0.47 把"能编译、能运行"提升为"能开发、能调试"。每项能力必须至少服务下列一类目标，
并满足三条约束：

1. **不改语言**：语言版本、opcode 集合、诊断码集合、零 pass 基线与 ABI 不变。
2. **不引入非确定性**：同输入同输出，工具输出可做逐字节比较。
3. **不扩大公共接口**：产品交付入口仍是 `tc-vm` 与 `tc-aot` 两个 CLI；`tc-reduce` 是
   `tests/tools/` 下的开发脚本，不安装、不分发；GDB RSP 是回环本机开发接口，不是
   产品 API。内部 API 不安装、不导出。

| 目标 | 含义 | 本分支能力 |
| ---- | ---- | ---- |
| 可观测 | 看见编译产物与执行过程 | IR dump、执行 trace、统计、trap 详情 |
| 可定位 | 缺陷落到源码位置 | 行表/DWARF、AOT 侧 gdb、VM 侧 gdb、抽象帧栈 |
| 可复现 | 同输入同字节输出 | 路径归一化、确定性 trace、调试信息可复现 |
| 可自动化 | CI 与本地同一入口 | 诊断 JSON、CTest 标签、预设、失败最小化器 |

工具输出面向两类使用者：**人**（读 trace、单步、下断点）与**工具**（CI、编辑器、最小化器、
差分脚本）。两者共用同一份输出契约，不各自定义格式。

## 2. 非目标

| 非目标 | 理由 |
| ---- | ---- |
| 优化管线（SSA/regalloc/内联） | 不属于工具链；会破坏零 pass 基线 |
| 公共嵌入 API、调试器 SDK、C 宿主库 | 违反"私有 core + 两个 CLI"契约 |
| GUI、可视化时间线、Web 调试前端 | 交付面过大，不进入本分支 |
| 完整 LSP / IDE 插件 | 只提供诊断 JSON，编辑器适配留在外置工具 |
| 语言特性、opcode、诊断码增删 | 语言版本冻结在 0.0.46 |
| 符号级表达式求值（`print`/`call`） | IR 无源码变量与调用协议；用 `monitor` 精确读取 |
| VM 反汇编视图 | IR 不是目标机器码；以 `monitor ir` 文本替代 |
| 把 qemu-user 纳入门禁 | 维持既有平台与 CI 策略 |
| RTOS/中断/并发 | 移交 0.0.48 |

## 3. 版本模型

0.0.47 是**工具链版本**，不是语言版本。两者必须可区分，避免把工具链增强误读为语义变化。

| 名称 | 取值 | 载体 | 说明 |
| ---- | ---- | ---- | ---- |
| 语言/契约版本 | `0.0.46` / `1` | `src/base/version.h`、契约检查 | 冻结，不随本分支变化 |
| 工具链版本 | `0.0.47` | 新增 `TC_TOOLCHAIN_VERSION`、CMake 工程版本 | 本分支交付 |
| 规范文档 | `*-0.0.46.md` | 标准/设计/测试计划 | 保持冻结；本文件是 0.0.47 工具链的唯一范围来源 |

- `tc-vm`/`tc-aot --version` 同时打印两者，例如
  `tc-vm 0.0.47 (language 0.0.46; reference VM)`。
- 契约检查继续断言 `TC_LANGUAGE_VERSION == "0.0.46"` 与 51 个 opcode；工具链版本不参与
  契约检查。
- 发布审计按"工具链版本 0.0.47、语言版本 0.0.46"分别断言；不得用单一版本号同时代表两者。
- CMake 工程版本升到 0.0.47 后，发布审计中的版本断言同步更新，语言版本断言保持不变。

## 4. 里程碑

依赖列中的 `→` 表示必须先完成；`∥` 表示可并行。

| 编号 | 交付物 | 依赖 | 验收 | 成本 |
| ---- | ---- | ---- | ---- | ---- |
| **T0** 治理 | 版本模型贯通；本文件进入文档地图；`CHANGELOG` 增加 0.0.47 条目；发布审计版本断言拆分 | — | 全量门禁除版本断言外零回归；`docs.cleanup` 通过 | 1 天 |
| **T1** 可观测 | `--dump-ir`、`--trace`（IR/call/mem/io/trap）、`--stats`、trap 详情 | T0 | trace 重复运行字节一致；正常 stdout 与退出码不变；新增 golden | 3–5 天 |
| **T2** 源级映射 | AOT `--debug` 发射 `.loc` 与行表；路径归一化；qemu gdbstub 冒烟；`--dump-linemap` | T1 | 每条 IR 指令可映射到源码 span；非 debug 产物字节不变；gdb 断点脚本可复现 | 1–2 周 |
| **T3** VM 调试 | 观察者 API、合成代码地址、GDB RSP v1（§6）、协议测试客户端 | T1；调试 ELF 另需 T2 | 协议套件通过；断点/单步/内存读/退出状态可断言；无观察者时零开销 | 2–3 周 |
| **T4** 机器接口 | `--diag-format=json`（JSONL）；诊断 schema；与 80 码表一致 | T1 | 与文本诊断同序同码；schema 稳定并有 golden | 3 天 |
| **T5** 测试基建 | CMakePresets、CTest 标签、`-L fast`、统一 `tests/run.py`、`tc-reduce` 最小化器 | ∥ T1 | CI 与本地同入口；标签覆盖全部用例；最小化器对种子缺陷收敛 | 3–5 天 |
| **T6** 可复现发布 | 调试产物可复现（路径/时间戳归一）、发布审计新增工具链检查 | T2 → T3 | 连续两次构建字节一致；审计项通过 | 2 天 |

### 4.1 交付物与实现落点

| 里程碑 | 主要落点 |
| ---- | ---- |
| T0 | `src/base/version.h`（`TC_TOOLCHAIN_VERSION`）、`CMakeLists.txt`、`src/driver/cli.c`（版本输出）、`tests/tools/s10_release_audit.py`（版本断言）、`CHANGELOG*.md` |
| T1 | `src/driver/cli.[ch]`（新开关）、`src/driver/frontend_driver.c`（执行钩子）、`src/vm/observe.[ch]`（新增）、`src/vm/interp.c`（回调点）、`src/ir/print.[ch]`（复用）、`src/backend/*/emit.c`（静态统计） |
| T2 | `src/debug/linemap.[ch]`（新增，行表与 `.file`/`.loc` 序列化）、`src/backend/{aarch64,arm32,riscv32}/emit.c`（每条 IR 指令前的 `.loc`）、`src/driver/aot_driver.c`（`--debug`/路径映射）、`src/driver/cli.c` |
| T3 | `src/vm/observe.[ch]`、`src/vm/gdbstub.[ch]`、`src/debug/dbgelf.[ch]`（新增；调试 ELF，§6.8）、`src/vm/vm.h`（调试入口）、`src/driver/tc-vm.c`（`--gdb`）、`tests/conformance/tools/`（协议用例）、`tests/tools/vm_gdb_client.py`（新增） |
| T4 | `src/diagnostic/diagnostic.[ch]`（JSON 写入）、`src/driver/cli.c`、诊断 golden |
| T5 | `CMakePresets.json`、`tests/CMakeLists.txt`（标签）、`tests/run.py`、`tests/tools/tc_reduce.py` |
| T6 | `src/driver/aot_driver.c`（路径归一/编译目录）、`tests/tools/s10_release_audit.py`（工具链产物检查）、确定性套件扩展 |

行表生成器必须是单一实现：AOT 的 `.loc`、trace 的 `span` 字段、VM 调试 ELF 与
`--dump-linemap` 输出都调用同一模块，禁止各后端自行拼接行号。

### 4.2 成本与关键路径

- 表中区间直接相加为 27–41 人日（约 5.5–8 周）；计入评审、门禁与文档收尾，单人串行
  按 7–9 周计；T5 可与 T1 并行，压缩后约 7 周。
- 关键路径：T1 → T2 → T3；T2 的外部工具链（clang/lld/qemu/gdb）是主要不确定源。
- 估算不含外部工具链版本排障；该部分在 §8.1 的 spike 中先行测量。

## 5. 接口契约

### 5.1 输出通道与退出码

- `stdout` 只承载被运行程序的输出，以及 `--help`/`--version`。
- 工具自身输出（IR dump、trace、stats、调试服务信息）一律走 `stderr` 或显式路径；保证
  stdout 字节在有无工具输出时完全一致。
- 工具输出必须确定性：默认不含绝对路径、时钟、PID、随机数、宿主地址；地址一律打印为区名
  加区内偏移。`--trace-paths=abs` 可显式关闭 trace（文本与 JSONL）的路径归一化，
  仅用于本地排查。
- 所有新增开关默认关闭；不带开关时 `tc-vm`/`tc-aot` 的行为、产物字节与退出码与本分支
  之前一致。
- **退出码不新增**：沿用 `0`（成功）、`1`（语言诊断或 trap）、`2`（用法错误）、`70`
  （能力/环境不可用）、`71`（内部错误）。工具链故障不得借用语言诊断码表达。
- 工具不适用的开关必须显式拒绝（退出 2），不得静默忽略；这与"部分能力不得写成已支持"
  的既有原则一致。

### 5.2 工具开关矩阵

| 开关 | `tc-vm` | `tc-aot` | 说明 |
| ---- | ---- | ---- | ---- |
| `--dump-ir[=<path>]` | ✓ | ✓ | IR 最终化后输出 |
| `--trace=<kinds>` | ✓ | 拒绝（2） | 指令级观察只属于 VM |
| `--stats[=<path>]` | ✓ 运行时统计 | ✓ 静态统计 | 两者字段不同（§5.5） |
| `--diag-format=text\|json` | ✓ | ✓ | 诊断输出格式 |
| `--debug` | 拒绝（2） | ✓ | 目标代码调试信息；VM 用 `--gdb-elf-out` |
| `--debug-path-map=<from>=<to>` | ✓ | ✓ | 归一化前缀重映射，作用于 §5.9 的四处消费者（按工具适用面） |
| `--dump-linemap[=<path>]` | ✓ | ✓ | 共享行表导出（§5.10） |
| `--gdb[=<port>]` | ✓ | 拒绝（2） | VM 调试会话 |
| `--gdb-elf-out=<path>` | ✓ | 拒绝（2） | 可选调试 ELF（§6.8） |
| `--gdb-allow-write` | ✓ | 拒绝（2） | 默认禁写 |
| `--checked` | ✓ | ✓ | 既有语义，不变 |

辅助开关随主开关的适用面：`--trace-out`/`--trace-format`/`--trace-limit`/`--trace-paths`
仅在 `tc-vm` 上有效；`--stats-out` 随 `--stats` 适用于两个工具。主开关被拒时，配套的
辅助开关同样退出 2；辅助开关未伴随对应主开关时（如 `--stats-out` 无 `--stats`、
`--trace-out` 无 `--trace`）同样退出 2。

### 5.3 `--dump-ir[=<path>]`

- 复用 `src/ir/print.h` 的确定性文本；默认写 `stderr`。
- 打印时机：IR 最终化之后、执行/代码生成之前。
- `--syntax-only`/`--sem-only` 下 IR 尚未最终化，无 IR 可打印，退出码维持部分检查语义；
  `--static-only` 含 CT 与 IR 最终化，`--dump-ir` 正常输出。

### 5.4 `--trace=<kinds>`

- `kinds` 表示访问/事件族，逗号分隔：
  - `ir`：每条 IR 指令一条记录；
  - `call`：进入/返回，含抽象深度与被调函数；
  - `mem`：`load`、`store`（含 `.volatile` 形态）与 `memmove`，含区名、区内偏移、字节数；
  - `io`：`read`/`write` 提交，含字节数与结果；
  - `trap`：trap 详情。
- `op` 字段承载完整打印名，包含 `.wrap`/`.ieee`/`.volatile`/`.nsw`/`.nuw` 后缀；
  `kinds` 只是过滤族，不改变打印名。
- `--trace-out=<path>`：默认 `stderr`；`--trace-format=text|jsonl`。
- `--trace-limit=<n>`：最多输出 n 条记录，`0`（默认）表示不限；截断时输出
  `# tc-trace truncated <n>` 后继续执行，不改变程序行为。

记录字段（文本与 JSONL 同名）：

| 字段 | 类型 | 说明 |
| ---- | ---- | ---- |
| `seq` | 整数 | 从 1 递增，跨 kinds 共用 |
| `depth` | 整数 | 抽象调用深度 |
| `func` | 字符串 | IR 函数名；顶层单元为 `top`（与 IR dump 一致）；不同模块同名时打印为 `<name>#<func_id>` |
| `bb` | 整数 | 基本块序号 |
| `insn` | 整数 | 块内指令序号 |
| `op` | 字符串 | opcode |
| `span` | 字符串 | `file:line:col`（归一化路径） |
| `detail` | 对象 | 仅 `call`/`mem`/`io`/`trap`：被调函数、区/偏移/长度、I/O 结果 |

文本格式首行 `# tc-trace 1 <tool> <profile> <source>`，末行
`# tc-trace end <records> <status>`；`detail` 在文本中为 `key:value` 逗号列表，
`detail` 中地址与偏移用 `0x` 十六进制，长度、大小、计数用十进制；顶层字段
（`seq`/`depth`/`bb`/`insn`）与 JSON 数值统一为十进制。示例：

```
# tc-trace 1 tc-vm aarch64-linux-user demo.tc
[0001] depth=1 func=main bb=0 insn=0 op=copy span=demo.tc:3:5
[0002] depth=1 func=main bb=0 insn=1 op=load.volatile span=demo.tc:4:5 detail=region:device,offset:0x0,size:4
# tc-trace end 2 ok
```

对应 JSONL：

```json
{"seq":1,"depth":1,"func":"main","bb":0,"insn":0,"op":"copy","span":"demo.tc:3:5"}
{"seq":2,"depth":1,"func":"main","bb":0,"insn":1,"op":"load.volatile","span":"demo.tc:4:5","detail":{"region":"device","offset":0,"size":4}}
```

确定性：`seq` 单调；`span` 使用归一化路径；同一程序重复执行产生相同字节。

函数命名约定（所有工具输出共用：trace、行表、trap 报告、`monitor funcs`/`frames`/`objects`）：
IR 函数名；顶层单元为 `top`；不同模块存在同名函数时打印为 `<name>#<func_id>`。
顶层单元的 `depth` 固定为 0，函数体从 1 开始（示例中的 `main` 为 1）。

### 5.5 `--stats`

- `tc-vm --stats`：运行时统计——执行指令总数、按 op 计数、最大抽象深度、五区使用峰值、
  分配次数、I/O 字节数；程序终止后输出，trap 终止同样输出并标注首个 `TC_RE_*`。
- `tc-aot --stats`：静态统计——函数数、IR 指令数、最大帧大小、静态/设备/代码区需求、
  按 op 计数；不依赖外部工具链，`--emit=asm` 下同样可用。
- 输出到 `stderr`（`--stats-out=<path>` 可改）；首行 `# tc-stats 1 <tool> <profile>`，
  字段顺序固定、只增不改义，供脚本解析。

### 5.6 trap 报告

`tc-vm` 在既有 `error[RT]` 报告上补齐：诊断码、`span`、函数名、基本块与指令序号、opcode、
抽象深度；函数名按 §5.4 约定。字段顺序固定、只增不改义；与 `--trace=trap` 的 `detail`
同源。
`--diag-format=json` 下 trap 同样输出一行 JSON，`code` 为 `TC_RE_*`；该行在基础字段之后
追加 `func`、`bb`、`insn`、`op`、`depth`，追加字段只增不减。

### 5.7 诊断机器接口

- `--diag-format=text|json`（默认 `text`）。`json` 为 JSONL，每行：

```json
{"code":"TC_CE_SYNTAX","severity":"error","message":"...","file":"demo.tc","line":3,"col":5,"length":2}
```

- 静态诊断与 RT trap 都使用该格式；trap 行的 `file`/`line`/`col` 取自触发指令的 span。
- 文本与 JSON 必须同序、同码、同位置；顺序由标准 §12.2 首诊总序决定，不由输出格式决定。
- 文件名使用 §5.9 的归一化规则；`json` 模式不输出颜色与人类说明文字。
- 字段只增不减：同一格式版本内新增字段必须追加，删除或改义需提升格式版本。

### 5.8 源级映射（AOT）

- `tc-aot --debug` 生成带调试信息的汇编：为每个源文件发射 `.file` 表，并在每组 IR 指令
  之前发射 `.loc <file> <line> <col>`；同时新增 `# source: <file>:<line>` 注释行（当前
  后端汇编不含源码注释，`--debug` 下为新增输出；非 `--debug` 时不发射），便于无 DWARF
  场景对照。
- 一条 IR 指令 lowering 成多条目标指令时，这些目标指令共享同一个 `.loc`；
  映射粒度是 IR 指令，不是目标指令。
- 汇编仍经 clang 集成汇编；`--emit=obj|elf` 时保留行表；`--emit=asm` 时 `.loc` 可见。
- `--emit=flat` 不承载调试段：`--debug` 仍生成带 `.loc` 的中间汇编与行表，最终 flat
  不含调试信息，此为文档化行为而非错误。
- 覆盖要求：行表覆盖 IR 全序的每条指令；同一函数内地址严格递增。行表是静态集合，
  `--trace=ir` 是动态执行序列（循环会重复、死代码不出现），两者不比较条目数；验收方式
  是对 trace 的每条记录按 `(func,bb,insn)` 查行表，断言其 `span` 与记录 `span` 相同。
- `tc-aot` 非 `--debug` 产物字节必须与 0.0.46 一致；该断言进入差分套件。
- 验证工具：`llvm-dwarfdump --debug-line` 或 `readelf --debug-dump=decodedline`；
  DWARF 版本随工具链，版本号记录在测试说明中。

### 5.9 路径归一化

- 源路径默认相对 root 模块所在目录与 `--include` 根归一化；无法归一时回退为
  `<basename>:<line>:<col>` 并标记 `external`。
- `--debug-path-map=<from>=<to>` 可重映射前缀，语义等价于编译器常见的调试前缀映射，
  用于构建机路径与开发机路径不同的场景；重映射在归一化之前应用。
- `--trace-paths=abs` 作用于 trace 的文本与 JSONL 输出；不影响行表、诊断 JSON 与
  调试 ELF。四者默认都走归一化路径。
- 归一化结果进入 trace、诊断 JSON、行表与调试 ELF 四处，必须一致；该一致性由共享行表
  测试覆盖。

### 5.10 共享行表产物

- 行表由单一模块生成（IR 指令序号 ↔ 源 span），供四处消费：AOT `.loc`、trace 记录、
  VM 调试 ELF（§6.8）、`--dump-linemap`。
- `--dump-linemap[=<path>]` 导出确定性 JSON，默认写 `stderr`：

```json
{"format":"tc-linemap","version":1,"profile":"aarch64-linux-user","source":"demo.tc",
 "entries":[{"func":"main","bb":0,"insn":0,"file":"demo.tc","line":3,"col":5}]}
```

- 条目顺序为 IR 最终化的全序；`func` 使用 §5.4 的函数命名约定；`vm_addr` 仅在 VM
  上下文出现（§6.2 的合成地址）。

### 5.11 输出格式版本策略

| 格式 | 版本标识 | 兼容规则 |
| ---- | ---- | ---- |
| trace 文本/JSONL | 首行 `# tc-trace 1` | 版本内只追加字段；改义需升版本 |
| 诊断 JSON | 每行 `code` 字段稳定 | 版本内只追加字段 |
| 行表 JSON | `"version":1` | 版本内只追加字段；条目顺序为全序 |
| 调试 ELF | 无版本字段 | 只承载符号与行表；不承诺向后兼容 |
| GDB RSP | 协议版本由 `qSupported` 协商 | 子集只增不减；废弃报文先返回空应答再移除 |
| `--stats` 文本 | 首行 `# tc-stats 1` | 字段只增不减；字段名与含义稳定 |
| trap 报告文本 | 沿用诊断文本格式 | 字段顺序固定、只增不改义；不单独版本化 |

### 5.12 `tc-reduce` 契约

- 用途：把触发某个诊断或 trap 的源程序缩到最小复现，服务于缺陷定位与回归语料。
- 调用：`tc-reduce <source.tc> -- <tool> <args...>`；工具参数中的 `{}` 表示候选源文件，
  缺省时追加到参数末尾。首次运行记录"失败指纹"：首个
  `TC_CE_*`/`TC_RE_*`、退出状态与终止方式；缩减过程中指纹必须保持不变。
- 缩减动作：删除整条语句/声明/未使用 `import`、简化字面量、删除不可达函数；
  每步之后重新解析，保持源文件可解析。
- 保证：输出行数或字节数严格减少；同输入同输出；`--timeout` 限制总时长。
- 非目标：不做语义等价化、不改 include 根、不合成新程序。

## 6. VM 调试器（GDB RSP）

### 6.1 会话模型

- 开关：`--gdb[=<port>]`，默认端口 0（由系统分配）。只绑定 `127.0.0.1`，不提供远程监听。
- 启动后 stub 在第一条指令执行前阻塞等待调试器连接；未连接时程序不推进，Ctrl-C 终止。
  连接成功后按调试器命令推进程序。
- 会话信息打印到 `stderr`：`tc-vm: gdbstub on 127.0.0.1:<port>`。
- 端口被占用或绑定失败：环境不可用，退出 70；`--gdb` 参数非法或对 `tc-aot` 使用：退出 2。
- 调试器经 TCP 通信，不占用程序 `stdin`；程序仍按标准读取自己的输入，`stdout` 不变。
- 调试器意外断开或校验和连续三次失败：程序停在当前指令边界，stub 关闭该连接并回到
  监听；重新连接后 `?` 按 §6.7 的状态报告：暂停中为 `T05thread:1;`，已终止为 `W<hex>`。
  进程不退出。
- RSP 是开发接口，不属于语言语义，也不进入差分门禁。

### 6.2 合成代码地址空间

VM 执行的是扁平 IR，没有目标机器码；为支持地址级断点与 `pc`，定义不参与语言值域的合成
代码地址：

```
TC_DEBUG_CODE_BASE = 0x5443_0000_0000
addr(insn) = TC_DEBUG_CODE_BASE + 4 * global_insn_index
```

- `global_insn_index` 是 IR 最终化后的全局单调指令序号，跨函数连续，确定且可复现。
- 该地址区间不属于任何平台档案的五个区，不进入 `funcaddr`/`bitcast` 结果，不参与程序行为；
  它是 64 位调试视图，不是目标地址，因此与 W 无关。W=32 档案的程序可见地址仍按 32 位呈现。
- 启动时校验合成区间与当前档案区表不重叠；重叠则实现错误退出 71，不得退化为歧义地址。
- `m` 对合成区间返回全零；断点由 stub 处理（`Z0`），不写内存。

### 6.3 报文子集（v1）

| 类别 | 报文 | 行为 |
| ---- | ---- | ---- |
| 握手 | `qSupported`、`QStartNoAckMode` | 报告 `PacketSize=4000;vContSupported+;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+`；支持免应答模式 |
| 停止原因 | `?` | `T05thread:1;`（断点/单步/暂停），`T02thread:1;`（中断），`T0bthread:1;`（TC trap），终止态 `W<hex>` |
| 中断 | `\x03`（带外字节，不是 `$` 包） | 运行中收到时完成当前指令并停在下一条边界，报告 `T02thread:1;`；未运行时忽略 |
| 寄存器 | `g`、`p<n>` | `p` 支持；`P` 默认返回 `E01`，`--gdb-allow-write` 下仅允许写 `pc` |
| 内存 | `m`、`M` | 读五区与合成代码区；写默认 `E01`，`--gdb-allow-write` 下仅限帧/静态/堆区 |
| 执行 | `s`、`S`、`c`、`C`、`vCont;s`、`vCont;c` | 单步、继续；单步粒度为一条 IR 指令；`S`/`C` 的信号参数忽略 |
| 断点 | `Z0`、`z0` | 指令边界断点，最多 64 个，超出返回 `E01`；`Z1`–`Z4` 返回空 |
| 线程 | `qC`、`qfThreadInfo`、`qsThreadInfo`、`Hc`/`Hg` | 单一合成线程 `1` |
| 特性 | `qXfer:features:read`、`qXfer:memory-map:read` | `target.xml`（4 个寄存器）与五区内存映射；支持分块读取 `m`/`l` 语义 |
| 监视 | `qRcmd` | §6.6 的 `monitor` 命令 |
| 生命周期 | `D`、`R`、`k`、`vKill` | `D` 分离后继续；`R` 复位到入口并保留断点；`k`/`vKill` 不支持 |
| 退出 | `W<hex>` | 程序自然终止或 trap 终止时以十六进制报告档案退出状态 |

- 帧格式按协议：`$<payload>#<checksum>`；在免应答模式之前，每个报文都需 `+`/`-` 应答。
- 未列出的报文一律返回空应答；不支持的写操作返回 `E01`，不得静默忽略。
- `\x03` 是带外中断字节，不属于报文，不受「未列出的报文」规则约束。
- 超过 `PacketSize` 的报文按协议丢弃并重新同步，不得崩溃或部分执行。
- 校验和错误的报文以 `-` 请求重发；同一报文连续三次失败按 §6.1 处理：关闭连接、
  程序停在当前指令边界并回到监听。

### 6.4 target.xml 与架构

```xml
<?xml version="1.0"?>
<target version="1.0">
  <feature name="org.tc.ir">
    <reg name="pc"    bitsize="64" regnum="0" type="code_ptr"/>
    <reg name="depth" bitsize="64" regnum="1" type="data_ptr"/>
    <reg name="sp"    bitsize="64" regnum="2" type="data_ptr"/>
    <reg name="insn"  bitsize="64" regnum="3" type="data_ptr"/>
  </feature>
</target>
```

| 寄存器 | 含义 |
| ---- | ---- |
| `pc` | 下一条待执行 IR 指令的合成地址 |
| `depth` | 当前抽象调用深度 |
| `sp` | 当前帧在帧区内的偏移 |
| `insn` | 已提交执行的 IR 指令数 |

`g`/`p` 按目标端序编码；现有档案均为小端，每个 64 位寄存器固定 16 个十六进制位（8 字节）。
未来引入大端档案时按 gdb 的目标端序约定重新确认（§8.1）。

- `target.xml` 不声明 `<architecture>`；架构由调试 ELF（若提供）决定，缺省时 gdb 使用其
  默认架构。寄存器集完全由本描述定义。
- 不暴露逐个 vreg 寄存器：IR 没有目标机寄存器分配，暴露伪寄存器会误导调试者；
  vreg 通过 `monitor vreg` 读取。
- 自定义架构与寄存器集在 gdb 各版本的行为差异属于 §8 风险，由 §8.1 的 spike 验证。

### 6.5 内存视图

- `qXfer:memory-map:read` 返回五区（帧/静态/设备/堆/代码）基址与长度，来自当前平台档案。
- `m` 读取不触发程序语义：直接读取区镜像，不消费设备输入、不改变指针状态、不产生 trap。
- 代码区在 VM 中只承载函数地址语义，没有指令字节；读取返回全零，函数定位用
  `monitor funcs`。
- 设备区默认可读但不可写；`--gdb-allow-write` 也不放开设备区写。
- 写帧/静态/堆区可能破坏语言不变量；checked build 在后续访问处按既有规则 trap，
  调试器会话不因此改变必选 trap 集合。

### 6.6 监视命令

`qRcmd` 子集，输出为十六进制编码文本：

| 命令 | 内容 |
| ---- | ---- |
| `monitor regions` | 五区基址/长度/当前使用量 |
| `monitor funcs` | 函数表：`funcaddr` 值、名称（按 §5.4 约定）、指令区间、合成地址范围 |
| `monitor objects` | 对象表：对象 id、区、偏移、大小、生命期状态 |
| `monitor frames` | 抽象帧栈：深度、函数、指令序号、源码位置 |
| `monitor next` | 执行到下一条同深度指令，跨越整个 `call` |
| `monitor finish` | 运行到当前帧返回（深度减一） |
| `monitor ir [<addr>]` | 指定地址或当前 pc 的 IR 文本（含操作数与 span） |
| `monitor vreg [<func>:]<name>` | 读取活动帧的 vreg 值（标量与指针按 W 打印）；省略 `<func>` 时在当前函数内解析，未找到返回 `E01` |
| `monitor stats` | 与 `--stats` 相同的运行时计数 |
| `monitor trap` | 最近一次 TC trap 的码、span、指令 |
| `monitor help` | 命令列表 |

- vreg 只存在于活动帧；跨函数同名必须写全 `monitor vreg <func>:<name>`，不支持读取
  调用者帧。
- 未知命令返回 `E01` 并附 `monitor help` 提示；命令解析失败不影响程序状态。
- 输出为纯文本，行尾 `\n`，不依赖终端宽度；同一状态重复查询结果一致。

### 6.7 断点、单步与继续语义

- 断点只允许落在指令边界（4 字节对齐）；未对齐地址返回 `E01`。
- `s`：执行一条 IR 指令。`call` 在调用者 CFG 中是单条原子指令，`s` 停在 `call` 之前，
  下一次 `s` 进入被调函数首条指令（`depth` 加一）。
- `c`：运行到断点、TC trap、程序终止，或被 `\x03` 中断（停在下一条指令边界并报告
  `T02thread:1;`）。
- 跨越调用用 `monitor next`，返回用 `monitor finish`；gdb 的 `next`/`finish` 依赖架构识别
  与栈展开，不作为验收项（§8）。
- TC trap 先完成 §12.5.2 的提交语义（trap 生效、已提交的 I/O 不回滚），再停止并报告
  `T0bthread:1;`；此时程序已进入终止态，`monitor trap` 可读取 trap 详情。
- 程序终止（自然终止或 trap 终止）后 stub 返回 `W<hex>`；退出状态取档案退出策略，
  与无调试器运行完全一致。终止态下 `s`/`c` 不执行新指令，直接再回 `W<hex>`；`R`
  可复位重跑。
- 调试暂停不产生程序可观察效果：暂停期间不推进、不超时、不提交 I/O。
- 运行期间新增/删除断点立即生效；命中已删除断点的陈旧地址不得触发停止。

### 6.8 与 AOT 调试视图的一致性

- AOT 与 VM 共用 §5.10 的行表生成器；同一源程序的 `file:line:col → 指令` 映射一致。
- VM 可选生成**调试 ELF**（`--gdb-elf-out=<path>`）：只承载符号与行表，地址使用合成代码
  地址，不含可执行代码。gdb 以 `file <elf>` + `target remote` 获得源级断点时，
  断点地址与 stub 的合成地址一致。
- 调试 ELF 的机器类型与链接方式（段基址等于 §6.2 的合成基址）在 §8.1 spike 中定案；
  目标文件格式只服务于 gdb 加载符号，不用于执行、反汇编或分发。
- `--gdb-elf-out` 可与 `--gdb` 同用，也可单独使用（生成 ELF 后正常执行），便于先产出
  调试 ELF 再启动会话。
- 调试 ELF 的生成复用 AOT 的 `.loc` 管线；两者不一致视为缺陷，由共享行表测试覆盖。
- 调试 ELF 是可选增量，不阻塞 T3 验收；无调试 ELF 时 gdb 仍可按合成地址下断点。

### 6.9 安全与语义约束

- stub 只读程序状态；唯一可写对象是 `pc`（`--gdb-allow-write`），用于跳转到指令边界。
- stub 不得改变 stdout 字节、trap 选择与退出状态；有/无调试器的两次运行必须字节一致。
- stub 不得把宿主地址泄漏为目标观察值；程序可见地址按目标字长 W 呈现，合成调试地址
  按 §6.2 的 64 位调试视图呈现，两者不得混用。
- 单步与断点检查在必选 trap 之前完成；trap 本身仍按 §12.5.2 提交语义执行。
- 威胁模型：只服务本机开发者，绑定回环、无认证、单客户端；不支持远程调试，
  文档不得把它描述成可远程使用的调试服务。

### 6.10 测试策略

- 仓库内提供最小 RSP 客户端（Python，`tests/tools/vm_gdb_client.py`），覆盖：
  帧与校验和、免应答模式、分块 `qXfer`、断点命中地址、单步计数、`monitor next` 跨调用、
  `monitor finish` 返回、运行中 `\x03` 中断（停在边界并报 `T02`）、寄存器读写（`P` 默认
  `E01`、allow-write 写 `pc`）、内存写拒绝与区边界、`R` 复位重跑、`Z1`–`Z4` 空应答、
  各 `monitor` 命令、trap 停止码、退出状态、`D` 分离后继续、坏校验和重同步。
- 上述协议套件不依赖真实 gdb，进入常规 `ctest`。
- 真实 gdb 端到端脚本作为可选测试：gdb 不存在时显式跳过并记录原因，不记为通过。
- LLDB 使用同一 RSP，作为机会性验证，不进入门禁。
- 语义一致性测试：同一程序在无调试器、仅连接后 `continue`、断点暂停多次后 `continue`
  三种场景下，stdout 字节、首个 trap 与退出状态必须一致。

### 6.11 交互示例

```sh
$ tc-vm --gdb demo.tc
tc-vm: gdbstub on 127.0.0.1:53412
# 另开终端；有调试 ELF 时：
$ gdb -q
(gdb) file demo.debug.elf
(gdb) target remote 127.0.0.1:53412
(gdb) break demo.tc:7
(gdb) continue
# 无调试 ELF 时按合成地址下断点：
(gdb) break *0x544300000010
(gdb) monitor frames
(gdb) monitor objects
(gdb) continue
```

LLDB 等价入口：`lldb -o "gdb-remote 127.0.0.1:53412"`。

## 7. 验收与门禁

- 每个里程碑的验收命令写入门禁记录（`docs/gates/`，自 G11 起）；门禁记录保持一页以内，
  只含结论、范围、证据入口、复现命令与当前限制。
- 新增能力不得降低既有门禁：`ctest` 全量、golden、确定性、`docs.cleanup` 与发布审计
  全部保持通过。
- 语言门禁不变：工具链测试不得断言与标准冲突的语义；协议测试只断言协议行为。
- 调试接口不进入 VM↔AOT 差分：差分仍比较程序 stdout、首个 trap 与退出状态。

### 7.1 新增测试项与标签

| 测试项 | 内容 | 标签 |
| ---- | ---- | ---- |
| `toolchain.ir_dump` | `--dump-ir` 文本 golden，两个工具一致 | `toolchain`, `fast` |
| `toolchain.trace_golden` | trace 文本/JSONL golden；重复运行字节一致 | `toolchain`, `fast` |
| `toolchain.stats` | VM 运行时统计与 AOT 静态统计字段 | `toolchain`, `fast` |
| `toolchain.diag_json` | 文本/JSON 同序同码同位置 | `toolchain`, `fast` |
| `toolchain.linemap` | 行表覆盖 IR 全序、trace 逐条 span 比对、路径归一化 | `toolchain` |
| `toolchain.debug_invariance` | 非 `--debug` 产物字节与基线一致 | `toolchain`, `aot` |
| `vm.gdb.protocol` | RSP 握手/断点/单步/`\x03` 中断/寄存器读写/内存写禁区/`R` 复位/`monitor`/退出/重同步 | `toolchain` |
| `vm.gdb.semantics` | 有/无调试器三种场景语义一致 | `toolchain`, `slow` |
| `toolchain.reduce` | 最小化器收敛且指纹不变 | `toolchain`, `slow` |
| `toolchain.presets` | CMakePresets 可配置 + `-L fast` 子集通过 | `toolchain`, `fast` |

- 标签集合固定为 `unit`、`conformance`、`toolchain`、`aot`、`slow`、`fast`；每个测试至少
  一个标签，`fast` 子集目标两分钟内完成。
- 可选外部依赖（gdb、qemu、clang、lld）缺失时按既有策略：配置期失败或测试显式跳过，
  不得静默通过。

### 7.2 0.0.47 退出标准

- T0–T6 全部完成，G11 PASS，`docs/gates/README.md` 更新。
- 既有门禁零回归：`ctest` 全量、golden、确定性、`docs.cleanup`、发布审计。
- 新增测试项全部进入 `ctest`；`fast` 子集在两分钟内通过。
- 双版本模型生效：`--version` 打印工具链 0.0.47 与语言 0.0.46；契约检查仍断言 51 opcode。
- 可复现：连续两次 `--debug` 构建的 ELF 字节一致，连续两次 flat 构建字节一致。
- 文档同步：本文件、`docs/README.md`、`AGENTS.md` 导航、`tests/README.md`、`src/README.md`
  （新增 `src/debug/` 模块边界）、`CHANGELOG`。

## 8. 风险与限制

| 风险 | 处理 |
| ---- | ---- |
| gdb 版本差异（ack 模式、`vCont`、`qXfer` 分块、自定义寄存器集） | 以最小子集实现；协议套件自测；§8.1 spike 给出兼容矩阵 |
| gdb 的 `next`/`finish` 依赖架构与栈展开 | 不作为验收项；以 `monitor next`/`monitor finish` 提供等价能力 |
| VM 单步性能（每指令一次往返） | 单步仅用于定位；批量观察用 `--trace`；失控由调试器侧 `\x03` 中断停在指令边界 |
| 合成地址与语言值域混淆 | 合成区间独立且文档化；调试 ELF 与 trace 均标注为调试视图 |
| 调试 ELF 的机器类型/链接方式与 gdb 兼容性 | §8.1 spike 先定案；失败时降级为"仅合成地址调试"，不阻塞 T3 |
| 行表与实际执行不一致（call 原子性、无内联） | 行表以 IR 指令为单位；以 trace 记录逐条比对 span；call 映射到调用点 |
| 调试写内存破坏不变量 | 默认禁写；仅 `--gdb-allow-write` 开放，并限定区 |
| 调试信息破坏可复现 | 路径归一化 + 无时间戳 + 固定编译目录；T6 断言两次构建字节一致 |
| trace 体积过大 | `--trace-limit` 截断并标记；默认关闭 |
| macOS 缺少 gdb | 协议套件不依赖 gdb；端到端测试显式跳过并报告；LLDB 机会性验证 |

### 8.1 开工前 spike

T3 开始前用 1–2 天确认以下事实，结论写入本文件对应小节，不改变里程碑边界：

1. 自定义 `target.xml`（无 `<architecture>`、4 个自定义寄存器）在目标 gdb 版本上的行为；
2. 显式指定架构时的寄存器集与断点行为是否仍可用；
3. 调试 ELF 的可行机器类型、段基址设置与 gdb 符号加载结果；
4. `qXfer` 分块、`QStartNoAckMode`、`vCont` 在目标 gdb 版本上的实际交互；
5. LLDB 对同一 RSP 子集的可用性。

## 9. 0.0.48 移交

- RTOS 方向的三层划分（单线程平台能力、中断语义、并发与调度）与冻结约束分析移交
  0.0.48 的范围文档；本分支不预埋语言钩子。
- 本分支的 trace、行表与 VM 调试接口是 0.0.48 验证中断时间线与上下文切换的复用基础；
  0.0.48 不得为 RTOS 改动 0.0.47 已定的工具输出契约，只能增量扩展。
- 移交的可扩展点：trace `kinds` 只增不减、`detail` 为开放对象、`monitor` 命令只增不减、
  GDB RSP 子集只增不减。

# TC 开发调试工具链 0.0.47

## 0. 依据与范围

- 语言语义以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为唯一权威。本分支
  不修改语言标准：51 个 opcode、80 个诊断码、零 pass 优化基线、ABI 与平台档案全部冻结。
- 本文件规定 0.0.47 分支的工具链范围、接口契约、里程碑与验收。工具链能力属于编译器实现，
  不构成语言可观察行为；任何工具输出都不得改变程序的标准语义。
- RTOS 能力（单线程平台扩充、中断语义、并发与调度）不在本分支范围，移交 0.0.48（§9）。
- 编译器入口仍是 `tc-vm` 与 `tc-aot`；0.0.47 新增工程工具 `tc`（初始化/测试，不提供用户
  entry 的通用编译操作；`--test` 内部编译测试入口）。工程布局、`tc.project`、根锚点与工程内
  测试见 [TC 工程模型 0.0.47](TC工程模型-0.0.47.md)。不新增公共 C 接口、嵌入 API 或调试器 SDK。

## 1. 目标与约束

0.0.47 把"能编译、能运行"提升为"能开发、能调试"。每项能力必须至少服务下列一类目标，
并满足三条约束：

1. **不改语言**：语言版本、opcode 集合、诊断码集合、零 pass 基线与 ABI 不变。
2. **不引入非确定性**：同输入同输出，工具输出可做逐字节比较（`--gdb` 会话行的动态端口
   与显式 timeout 是否到期取决于运行时序，是例外，见 §5.1、§6.1）。
3. **不扩大公共接口**：编译器入口仍是 `tc-vm` 与 `tc-aot` 两个 CLI；`tc` 是 0.0.47 新增的
   工程工具，不提供用户 entry 的通用编译操作、不安装为库；`tc-reduce` 是 `tests/tools/` 下的
   开发脚本，不安装、不分发；GDB RSP 是回环本机开发接口，不是产品 API。内部 API 不安装、
   不导出。

| 目标 | 含义 | 本分支能力 |
| ---- | ---- | ---- |
| 可观测 | 看见编译产物与执行过程 | IR dump、执行 trace、统计、trap 详情 |
| 可定位 | 缺陷落到源码位置 | 行表/DWARF、AOT 侧 gdb、VM 侧 gdb、抽象帧栈 |
| 可复现 | 同输入同字节输出 | 工程根相对路径、确定性 trace、调试信息可复现 |
| 可自动化 | CI 与本地同一入口 | 诊断 JSON、CTest 标签、预设、失败最小化器 |

工具输出面向两类使用者：**人**（读 trace、单步、下断点）与**工具**（CI、编辑器、最小化器、
差分脚本）。两者共用同一份输出契约，不各自定义格式。

## 2. 非目标

| 非目标 | 理由 |
| ---- | ---- |
| 优化管线（SSA/regalloc/内联） | 不属于工具链；会破坏零 pass 基线 |
| 公共嵌入 API、调试器 SDK、C 宿主库 | 违反"私有 core + 编译器两入口/工程工具边界"契约 |
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

- `tc`/`tc-vm`/`tc-aot --version` 同时打印两者，例如
  `tc 0.0.47 (language 0.0.46; project tool)`、
  `tc-vm 0.0.47 (language 0.0.46; reference VM)`、
  `tc-aot 0.0.47 (language 0.0.46; native AOT)`。
- 契约检查继续断言 `TC_LANGUAGE_VERSION == "0.0.46"` 与 51 个 opcode；工具链版本不参与
  契约检查。
- 发布审计按"工具链版本 0.0.47、语言版本 0.0.46"分别断言；不得用单一版本号同时代表两者。
- CMake 工程版本升到 0.0.47 后，发布审计中的版本断言同步更新，语言版本断言保持不变。

## 4. 里程碑

依赖列中的 `→` 表示必须先完成；`∥` 表示可并行。

| 编号 | 交付物 | 依赖 | 验收 |
| ---- | ---- | ---- | ---- |
| **T0** 治理 | 版本模型贯通；三工具入口契约（含 `TcImplError`→退出码统一映射）；本文件进入文档地图；`CHANGELOG` 增加 0.0.47 条目；发布审计版本断言拆分 | — | 全量门禁除版本断言与 §5.1 退出码期望更新外零回归；`docs.cleanup` 通过 |
| **T1** 可观测 | `--dump-ir`、`--trace`（IR/call/mem/io/trap）、`--stats`、trap 详情；新增输出路径错误复用 T0 映射 | T0 | trace 重复运行字节一致；正常/语言诊断路径的 stdout 与退出码不变；新增 golden |
| **T2** 源级映射 | AOT `--debug` 发射 `.loc` 与行表；根相对路径；qemu gdbstub 冒烟；`--dump-linemap` | T1 | 每条 IR 指令可映射到源码 span；路径根相对；跨根构建产物一致；gdb 断点脚本可复现 |
| **T3** VM 调试 | 观察者 API、合成代码地址、GDB RSP v1（§6）、协议测试客户端 | T1；调试 ELF 另需 T2 | 协议套件通过；断点/单步/内存读/退出状态可断言；无观察者时零开销 |
| **T4** 机器接口 | `--diag-format=json`（JSONL）；诊断 schema；与 80 码表一致 | T1 | 与文本诊断同序同码；schema 稳定并有 golden |
| **T5** 测试基建 | CMakePresets、CTest 标签、`-L fast`、统一 `tests/run.py`、`tc-reduce` 最小化器 | ∥ T1 | CI 与本地同入口；标签覆盖全部用例；最小化器对种子缺陷收敛 |
| **T6** 可复现发布 | 调试产物可复现（根相对路径/时间戳）、发布审计新增工具链检查 | T2 → T3 | 连续两次构建字节一致；审计项通过 |

工程模型（`tc`、`tc.project`、测试布局、根路径）是独立轨道 P0–P5，见
[TC 工程模型 0.0.47](TC工程模型-0.0.47.md) §11；P0 先于 T1，P2（路径模型）是 T1/T2
的前置，P4（AOT `.asciz`/`.file`/`.loc`/comp_dir）是 T2 的前置。本节 T1–T6 只列
调试/可观测能力，不重复工程工具本身。

### 4.1 交付物与实现落点

| 里程碑 | 主要落点 |
| ---- | ---- |
| T0 | `src/base/version.h`（`TC_TOOLCHAIN_VERSION`）、`CMakeLists.txt`、`src/driver/cli.[ch]`（版本输出、`tc_impl_error_exit`）、`src/driver/frontend_driver.c`、`src/driver/aot_driver.c`（改用统一映射）、`tests/tools/s10_release_audit.py`（版本断言与三入口）、退出码 CLI 测试、`CHANGELOG*.md` |
| T1 | `src/driver/cli.[ch]`（新开关）、`src/driver/frontend_driver.c`（执行钩子、输出路径错误统一映射）、`src/vm/observe.[ch]`（新增）、`src/vm/interp.c`（回调点）、`src/ir/print.[ch]`（复用）、`src/backend/*/emit.c`（静态统计） |
| T2 | `src/debug/linemap.[ch]`（新增，行表与 `.file`/`.loc` 序列化）、`src/backend/{aarch64,arm32,riscv32}/emit.{h,c}`（每条 IR 指令前的 `.loc`；可区分 OOM 状态）、`src/driver/aot_driver.c`（`--debug`/根相对路径/OOM→70）、`src/driver/cli.c` |
| T3 | `src/vm/observe.[ch]`、`src/vm/gdbstub.[ch]`、`src/debug/dbgelf.[ch]`（新增；调试 ELF，§6.8）、`src/vm/vm.h`（调试入口）、`src/vm/interp.c`/`src/vm/internal.h`（续行/帧快照/逐 vreg defined 状态）、`src/driver/tc-vm.c`（`--gdb`）、`tests/conformance/tools/`（协议用例）、`tests/tools/vm_gdb_client.py`（新增） |
| T4 | `src/diagnostic/diagnostic.[ch]`（JSON 写入）、`src/driver/cli.c`、诊断 golden |
| T5 | `CMakePresets.json`、`tests/CMakeLists.txt`（标签）、`tests/run.py`、`tests/tools/tc_reduce.py` |
| T6 | `src/driver/aot_driver.c`（路径归一/编译目录）、`tests/tools/s10_release_audit.py`（工具链产物检查）、确定性套件扩展 |

行表生成器必须是单一实现：AOT 的 `.loc`、trace 的 `span` 字段、VM 调试 ELF 与
`--dump-linemap` 输出都调用同一模块，禁止各后端自行拼接行号。工程工具与路径模型落点
见工程模型文档 §8（`src/driver/tc.c`、`src/project/*`、`src/base/path.*`）。

退出码映射按 T0/T1 分两步实现：

- **T0**：在 `src/driver/cli.[ch]` 增加私有 `tc_impl_error_exit(TcImplError)`，按 §5.1 完整
  映射；`frontend_driver.c` 的 `print_impl_error()` 与 `aot_driver.c` 的
  `report()`/`report_unavailable()` 全部改走该函数，删除“非 usage 一律 71”的本地分支，
  `TC_IMPL_UNAVAILABLE` 不得绕过映射。`tc-vm`、`tc-aot` 与后续 `tc`/worker、`run_source`
  共用该映射，不得各自实现。实现启动时同步驱动头注释与 `src/CONTRACTS.md`，并在
  `tests/tools/` 与 CLI 测试中覆盖 0/2/70/71。
- **T1**：新增 `--trace-out`、`--stats-out`、`--dump-ir` 的创建/写入失败统一走该映射（70），
  不得为可观测输出新增本地 71 分支；补测缺源、输出路径失败、AOT 外部工具/平台不可用→70，
  实际内存耗尽→70，文档化上限/内部错误→71，用法错误→2 的回归。T2/T3 的
  `--dump-linemap`、`--gdb-elf-out` 到对应里程碑实现时同样复用，不另设映射。

### 4.2 关键路径

- 关键路径：P0 → P2，T1 → T2 → T3；P4 是 T2 的 AOT/调试信息前置。
- T2 的外部工具链（clang/lld/qemu/gdb）是主要不确定源；其版本差异在 §8.1 的 spike 中
  先行确认，不改变里程碑边界。

## 5. 接口契约

### 5.1 输出通道与退出码

- `stdout` 承载：`tc-vm` 的被运行程序输出；`tc-aot` 默认 `--emit=asm` 且未给
  `--output` 时的目标汇编；`tc` 的 init/test 报告；`tc-reduce` 未给 `--output` 时的最终
  最佳源（§5.12）；以及 `--help`/`--version`。
  所有新增编译器工具输出（IR dump、trace、stats、诊断、调试服务信息）一律走 `stderr`
  或显式路径；`tc-vm` 的 stdout 字节与 `tc-aot` 的汇编字节不受这些工具输出影响。
  `tc` 的输出不属于编译器 stderr 排序流，格式见工程模型文档 §5–§6。
- `tc-aot --emit=obj|elf|flat` 仍要求显式 `--output=<path>`；缺失按用法错误退出 2。
  只有 `--emit=asm` 可省略 `--output` 并写 `stdout`。
- 多路输出共用 `stderr` 时按阶段时间序排列：编译期输出（诊断，其次 `--dump-ir`，再次
  `--dump-linemap`）→ 若编译成功并进入 VM，`--gdb` 会话行（见 §6.1）→ trace 记录流
  （含 footer）→ trap 报告行 → stats；显式 `--trace-out`/`--stats-out` 分流后互不交错，
  不参与排序。
- 工具输出必须确定性：不含绝对路径、时钟、PID、临时路径、随机数、宿主地址；源路径一律
  工程根相对（§5.9），程序可见地址打印为区名加区内偏移。GDB 合成代码地址与调试 ELF
  地址是 §6.2 定义的按档案 W 选择的合成调试视图（W=64/W=32 各自区间），不属于该地址规则。`--gdb` 会话行（§6.1）含系统
  分配的端口，以及 `tc --test`/`tc-reduce` 显式 timeout 是否到期，取决于运行时序，
  不参与逐字节比较；timeout 报告文本本身仍不含实际耗时、PID 或临时路径。
- JSON/JSONL 对象的键按各自 schema 示例的顺序输出；键顺序与字段顺序一样属于逐字节
  确定性契约。文本模式的行内字段解析规则见各小节。
- JSONL 只保证各自流内的记录是合法 JSON；同一 `stderr` 上的非目标输出（IR dump、
  linemap、`--gdb` 会话行、stats、文本 trap 等）仍按原格式出现。机器解析必须按 §5.1 的
  阶段顺序跳过非目标行，或用 `--trace-out`/`--stats-out`/`--dump-ir=<path>`/
  `--dump-linemap=<path>` 显式分流；`--diag-format=json` 不提供诊断专用输出路径。
- 新增开关默认关闭。0.0.47 按清洁设计**不保留 0.0.46 的绝对路径文本/产物字节**：
  程序 stdout、退出码、trap 选择与 ABI 语义不变；工具文本与产物中的源路径统一工程根
  相对（§5.9）。工程模型的默认路径行为不是开关。
- **退出码不新增**：沿用 `0`（成功）、`1`（语言诊断、trap 或测试失败/超时）、`2`（用法
  错误）、`70`（能力/环境/宿主 IO 不可用）、`71`（内部错误）。工具链故障不得借用语言诊断码
  表达；源读取/宿主 IO 与 `tc --test` 的同一失败映射一致。
- **`TcImplError` 完整映射**：`TC_IMPL_OK`→0（成功，不作为错误上报）；`TC_IMPL_USAGE`→2；
  `TC_IMPL_IO`、`TC_IMPL_UNAVAILABLE`、`TC_IMPL_OOM`→70；`TC_IMPL_INTERNAL`→71。源/导入
  读取、诊断/输出/临时文件写入等宿主 IO 失败，外部工具或平台能力不可用，以及实际内存
  耗尽均归 70；契约破坏与文档化实现上限归 71。所有入口共用同一私有映射函数，不得把非
  `TC_IMPL_USAGE` 的失败一律归为 71。
- `tc --test` 的 `--test-timeout=<seconds>` 是单测试 deadline 的唯一开关：到期计测试失败
  （1），非法取值（缺值、负数、带符号/空格/小数、非整数、溢出）退出 2，fork 失败退出 70，
  非超时的 worker 异常终止或结果协议损坏退出 71。timeout 报告不含实际耗时、PID 或临时路径。
- 开关取值非法（未知 `--trace` kind、未知 `--diag-format` 值、非法数值等）一律按用法
  错误退出 2（同 `--profile` 的既有行为），不得静默忽略。
- 无符号十进制参数（`--test-timeout`、`--trace-limit`、`--gdb=<port>`、`tc-reduce
  --timeout` 等）只接受 ASCII 数字，至少一位，不接收符号、空格或小数；先按 uint64
  解析，超过 `UINT64_MAX` 为溢出并退出 2；各开关再校验自己的值域（如端口 0–65535）。
  由秒换算的 deadline 饱和到可表示上限，不因乘法溢出提前到期。
- 本文件新增的可带值开关（`--trace`、`--trace-format`、`--trace-out`、`--trace-limit`、
  `--stats-out`、`--dump-ir`、`--dump-linemap`、`--gdb`、`--gdb-elf-out`、`--diag-format`）
  重复出现 → 2；`--include` 仍是唯一可重复的路径开关，至多 32 个，且与 manifest include
  合计不超过 32（工程模型 §3.2）；超出 → 2。无值开关（`--stats`、`--debug`、
  `--gdb-allow-write`）重复出现按幂等处理，不视为错误。
- 工具不适用的开关必须显式拒绝（退出 2），不得静默忽略；这与"部分能力不得写成已支持"
  的既有原则一致。
- 输出路径（`--trace-out`、`--stats-out`、`--dump-ir`、`--dump-linemap`、`--gdb-elf-out`、
  `--output`）无法创建或写入失败视为环境不可用，退出 70；路径参数形式非法退出 2。
- 输出目标身份不等同于字符串相等：已存在目标用 `fstat` 的 `(st_dev,st_ino)` 判断；
  不存在目标用规范化父目录 + 最终组件判断；symlink/hardlink 别名、输出与输出别名、输出与
  源文件/manifest/stdin 的别名均退出 2。写前复核与写入必须使用同一描述符，避免 TOCTOU。
- `tc-reduce --output` 的结果文件写入失败同样 70；其形式/重复规则见 §5.12。

### 5.2 工具开关矩阵

| 开关 | `tc-vm` | `tc-aot` | 说明 |
| ---- | ---- | ---- | ---- |
| `--dump-ir[=<path>]` | ✓ | ✓ | IR 最终化后输出 |
| `--trace=<kinds>` | ✓ | 拒绝（2） | 指令级观察只属于 VM |
| `--stats` | ✓ 运行时统计 | ✓ 静态统计 | 两者字段不同（§5.5） |
| `--diag-format=text\|json` | ✓ | ✓ | 诊断输出格式 |
| `--debug` | 拒绝（2） | ✓ | 目标代码调试信息；VM 用 `--gdb-elf-out` |
| `--dump-linemap[=<path>]` | ✓ | ✓ | 共享行表导出（§5.10） |
| `--gdb[=<port>]` | ✓ | 拒绝（2） | VM 调试会话 |
| `--gdb-elf-out=<path>` | ✓ | 拒绝（2） | 可选 VM 调试 ELF（内部 writer；`w32` 70，见 §6.8） |
| `--gdb-allow-write` | ✓ | 拒绝（2） | 默认禁写 |
| `--checked` | ✓ | ✓ | 既有语义，不变 |

辅助开关随主开关的适用面：`--trace-out`/`--trace-format`/`--trace-limit` 仅在
`tc-vm` 上有效；`--stats-out` 随 `--stats` 适用于两个工具；`--gdb-allow-write` 随
`--gdb`。主开关被拒时，配套的辅助开关同样退出 2；辅助开关未伴随对应主开关时
（如 `--stats-out` 无 `--stats`、`--trace-out` 无 `--trace`、`--gdb-allow-write` 无
`--gdb`）同样退出 2。根与源路径由 manifest/root 模块决定；输出路径由对应
`--*-out`/`--output` 显式给出；不提供绝对路径输出模式。工程内默认 `profile` 由
`tc.project` 提供，CLI `--profile` 覆盖；`tc --test` 使用 manifest profile，不调用 AOT。
`--test-timeout` 只属于 `tc --test`，不在本表；worker 不启用
trace/gdb/debug/stats/dump-* 等编译器观察开关。

#### 5.2.1 与部分检查的组合

| 开关 | `--syntax-only`/`--sem-only` | `--static-only`（`tc-aot`） | `--static-only`（`tc-vm`） | 正常模式 |
| ---- | ---- | ---- | ---- | ---- |
| `--dump-ir` | 2 | 正常 | 正常 | 正常 |
| `--stats` | 2 | 正常（仅 IR 派生字段） | 2（不执行） | 正常 |
| `--dump-linemap` | 2 | 正常 | 正常 | 正常 |
| `--gdb-elf-out` | 2 | 2（工具不适用） | 正常（只生成 ELF；`w32` 70） | 正常（仅 `tc-vm`；`w32` 70） |
| `--trace` | 2 | 2（工具不适用） | 2（不执行） | 正常（仅 `tc-vm`） |
| `--gdb` | 2 | 2（工具不适用） | 2（不执行） | 正常（仅 `tc-vm`） |
| `--debug` | 2 | 2（不生成代码） | 2（工具不适用） | 正常（仅 `tc-aot`） |
| `--diag-format` | 正常 | 正常 | 正常 | 正常 |

规则：请求的输出依赖尚未运行的阶段或尚未发生的执行时，按用法错误退出 2，不得静默跳过；
§5.2 中工具不适用的开关在 `tc-aot` 的所有模式下均退出 2（以 §5.2 为准）；表中未列出的
输出型开关按同一规则处理。部分检查模式下显式指定 `--emit` 或 `--output` 同样退出 2
（二者不产生任何产物；未显式指定时按默认值处理且不产生产物）。`--static-only` 本身的
退出码仍是部分检查语义（无诊断为 0，否则为 1）。

### 5.3 `--dump-ir[=<path>]`

- 复用 `src/ir/print.h` 的确定性文本；默认写 `stderr`。`modules:` 段的路径按 §5.9
  输出为工程根相对路径（不得出现绝对路径），其余内容与 `tc_ir_print` 一致。
- 打印时机：IR 最终化之后、执行/代码生成之前。
- 与部分检查开关的组合见 §5.2.1：`--syntax-only`/`--sem-only` 下没有最终 IR，`--dump-ir`
  退出 2；`--static-only` 含 CT 与 IR 最终化，正常输出。

### 5.4 `--trace=<kinds>`

- `kinds` 表示访问/事件族，逗号分隔，至少一个非空项；项为 `ir`/`call`/`mem`/`io`/`trap`，
  大小写敏感；空项或未知项 → 2，重复项按一次处理：
  - `ir`：每条 IR 指令一条记录；
  - `call`：进入/返回，含抽象深度与被调函数；
  - `mem`：`load`、`store`（含 `.volatile` 形态）与 `memmove`，含区名、区内偏移、字节数；
  - `io`：`read`/`write`/`writeln` 提交，含字节数与结果；`writeln` 是独立提交流水
    （附带换行），不是 `write` 的语法糖；
  - `trap`：trap 详情。
- `op` 字段承载完整打印名，包含 `.wrap`/`.ieee`/`.volatile`/`.nsw`/`.nuw` 后缀；
  `kinds` 只是过滤族，不改变打印名。
- `kinds` 的过滤先于计数：被过滤掉的事件不占 `seq`、不计入 `--trace-limit`，也不计入
  `dropped`。
- `--trace-out=<path>`：默认 `stderr`；`--trace-format=text|jsonl`，缺省 `text`。
- `--trace-limit=<n>`：最多输出 n 条数据记录（控制对象不计入），`0`（默认）表示不限；
  `<n>` 只含 ASCII 十进制数字、非负、不接收符号/空格/小数，溢出 → 2。首次截断时输出不带计数的
  `# tc-trace truncated` 后继续执行，不改变程序行为。最终丢弃数在流的末尾给出（见本节末）。

记录字段（文本与 JSONL 同名）：

| 字段 | 类型 | 说明 |
| ---- | ---- | ---- |
| `seq` | 整数 | 从 1 递增，只统计实际输出的数据记录；header/truncated/footer 不占 `seq` |
| `depth` | 整数 | 抽象调用深度 |
| `kind` | 字符串 | `unit`（顶层单元）或 `function`（普通函数） |
| `module` | 字符串 | 根相对显示模块路径（与 `span.file` 同一 `tc_path_display`；外部 `../`/回退 `./<basename>#<n>`）；顶层单元为 root 模块显示路径 |
| `func` | 字符串 | `kind=unit` 时为字面 `<top>`；`kind=function` 时为源码函数名；跨模块同名由 `module` 区分，不再拼接 `#func_id` |
| `bb` | 整数 | 基本块序号 |
| `insn` | 整数 | 块内指令序号 |
| `op` | 字符串 | opcode |
| `span` | 字符串 | `file:line:col`（工程根相对路径） |
| `detail` | 对象 | 仅 `call`/`mem`/`io`/`trap`；键名见下表 |

`kind` 是调试实体类型（`unit`/`function`），与 `--trace=<kinds>` 的事件族不是同一概念；
事件族由本记录的 `detail` 键集区分。

`detail` 的键名固定（新增键按 §5.11 只增不减，跨版本扩展见 §9）：

| 记录 | `detail` 键 |
| ---- | ---- |
| `call` | `direction`（`enter`/`return`）、`callee`（被调函数源码名）、`callee_module`（被调函数显示模块路径）；`callee` 总是 `kind=function`，顶层单元不可作为 callee |
| `mem` | `load`/`store`：`region`（区名）、`offset`（区内偏移）、`size`（字节数）；`memmove`：`dst_region`、`dst_offset`、`src_region`、`src_offset`、`size`（`n × sizeof T` 的总字节数） |
| `io` | `bytes`（本次提交字节数；`writeln` 含末尾换行）、`result`（`ok`/`eof`/`error`） |
| `trap` | `code`（`TC_RE_*`） |

- 非 `ir` 记录同样携带触发指令的 `op`、`bb`、`insn`、`span`：`call` 记录为 `call`，
  `mem` 为对应访问 op（含 `.volatile` 形态），`io` 为 `read`/`write`/`writeln`，
  `trap` 为触发 trap 的指令 op。记录类别由 `detail` 键集区分；`ir` 记录不带 `detail`。
- `detail` 的数值编码：文本模式 `offset`/`dst_offset`/`src_offset` 用 `0x` 十六进制，
  `size`/`bytes` 用十进制；JSONL 中这些键均为十进制 JSON 整数，不写成字符串。
- `region`/`dst_region`/`src_region` 取值与 `--dump-ir`/`monitor regions` 的五区名一致
  （`frame`/`static`/`device`/`heap`/`code`）；`memmove` 的 `size` 是 `n × sizeof T`
  的总字节数。
- `mem` 记录只在地址成功解析到有效区/对象之后输出；空指针、跨区、悬垂、未初始化读取等
  在解析出 `region` 前就 trap 的访问只输出 `trap` 记录，不输出 `mem`，也不伪造
  `region`/`offset`/`size`。`memmove` 的源或目标失败同样只输出 `trap`。

`call` 的 `enter` 与 `return` 记录都以调用点指令为位置锚点：`module`/`func`/`bb`/`insn`/`span`
指向调用者的 `call` 指令，`depth` 取该调用点处的调用者深度（不是被调函数的深度）；
`return` 记录另带 `direction:"return"`、`callee` 与 `callee_module`。被调函数的深度由随后的
`ir` 记录体现。
同一调用点上的记录顺序固定：当 `ir` 与 `call` 同时启用时，先输出 `op=call` 的 `ir`
记录，再输出 `enter`；只启用 `call` 时直接输出 `enter`。被调函数最后一条 `ir` 记录
（若启用了 `ir`）之后输出 `return`。正常返回时 `enter` 与 `return` 成对；被调函数发生
trap、程序终止或 `--trace-limit` 截断时不补发缺失的 `return`。
同一条指令产生多个启用的记录时，事件记录统一排在其 `ir` 记录之后，顺序为
`mem` → `io` → `call` → `trap`；未启用 `ir` 时事件记录直接输出。

调试显示命名约定（trace、行表、trap 报告、`monitor funcs`/`frames`/`vreg`/`trap` 共用）：
- `kind=unit`，`module=<root 模块显示路径>`，`func=<top>`：顶层单元。`<top>` 是字面 token，不是 TC 标识符；不改变标准 §2.3，也不禁止用户声明 `func top`。
- `kind=function`，`module=<模块显示路径>`，`func=<源码函数名>`：普通函数；不同模块同名由 `module` 区分，函数 `top` 仍显示为 `top`，没有额外名字特判。
- `module`/`callee_module`/`span.file` 使用同一显示路径**格式**；回退名统一以 `./` 保留前缀开头（见 §5.9）；`module` 是当前实体模块，`callee_module` 是被调函数模块，`span.file` 是当前记录的源码位置。跨模块调用时 `callee_module` 不必等于 `span.file`。外部 `./<basename>#<n>` 回退编号按 §5.9 在编译内稳定，不同编译模式可不同，不承诺跨模式稳定。
- `(kind,module,func,bb,insn)` 是 IR 指令/linemap 键；`kind` 区分单元/函数，`module` 区分跨模块同名成员。trace 记录以 `seq` 唯一，同一五元组可有多条记录（`ir`+事件、`call` 的 `enter`/`return`），trace→linemap 为多对一。
- 未进入当前调试面的模块成员继续按各自命名空间处理：`const`/`static var`/`struct` 若未来暴露，使用 `(member_kind,module,name)`；结构体字段使用 `(module,owner_struct,field_path)`；局部变量使用 `(module,function,scope,name)`。当前 trace/linemap/trap/monitor/ELF 只暴露 `unit`/`function`，不暴露这些名字。
- `TcFunc.id` 只作为 IR dump/AOT 内部符号与 `monitor ir` 的原始结构操作数（`f<id>`）出现；`monitor ir` 的其它结构操作数包括 `%N`、`struct#id`、label，它们都不是 trace/linemap/trap/monitor 的调试实体身份。`o<id>`/`c<id>` 只出现在完整 `--dump-ir` 的 `objects:`/`consts:` 段。`monitor objects` 的对象 id 同样是内部标识。
IR dump 的 `units`/`funcs`/`structs`/`consts`/`objects` 段使用结构化的 `top`/`f<id>`/`m<id>`/`struct#id`/`c<id>`/`o<id>`，不属于本显示命名，也不随本约定改变。
顶层单元的 `depth` 固定为 0，函数体从 1 开始（示例中的 `main` 为 1）。

文本格式首行 `# tc-trace 3 <tool> <profile> <source>`，末行
`# tc-trace end <records> <status>`；数据记录前缀 `[<seq>]` 为至少 4 位十进制零填充，
`seq` 超过 9999 时自然加宽；`detail` 为 `key:value` 逗号列表，其中地址与偏移用 `0x`
十六进制，长度、大小、计数用十进制；顶层数值字段 `seq`/`depth`/`bb`/`insn` 为十进制；
`kind`/`module`/`func`/`op`/`span` 为字符串；JSONL 中 `detail` 为对象。`records` 为实际输出的数据
记录数。header 的 `source` 是 `<profile>` 之后的整行剩余部分，可含空格。文本 `span` 为
`file:line:col`，`file` 可含 `:`，line/col 从右侧两个 `:` 解析。文本模式是面向人的视图；
路径含空格、`]` 或逗号时文本字段有歧义，机器解析必须使用 JSONL。示例：

```
# tc-trace 3 tc-vm aarch64-linux-user demo.tc
[0001] depth=1 kind=function module=demo.tc func=main bb=0 insn=0 op=copy span=demo.tc:3:5
[0002] depth=1 kind=function module=demo.tc func=main bb=0 insn=1 op=load.volatile span=demo.tc:4:5 detail=region:device,offset:0x0,size:4
# tc-trace end 2 ok
```

对应 JSONL：

```json
{"seq":1,"depth":1,"kind":"function","module":"demo.tc","func":"main","bb":0,"insn":0,"op":"copy","span":"demo.tc:3:5"}
{"seq":2,"depth":1,"kind":"function","module":"demo.tc","func":"main","bb":0,"insn":1,"op":"load.volatile","span":"demo.tc:4:5","detail":{"region":"device","offset":0,"size":4}}
```

JSONL 不使用 `#` 控制行，头、截断与尾以带 `type` 的对象表达：

```json
{"type":"header","format":"tc-trace","version":3,"tool":"tc-vm","profile":"aarch64-linux-user","source":"demo.tc"}
{"type":"truncated"}
{"type":"footer","records":N,"dropped":M,"status":"ok"}
```

顶层单元与函数 `top` 的显示形态（`<top>` 为字面 token）：

```
[0003] depth=0 kind=unit module=demo.tc func=<top> bb=0 insn=0 op=call span=demo.tc:2:5
[0004] depth=1 kind=function module=lib.tc func=top bb=0 insn=0 op=copy span=lib.tc:2:5
```

- `records` 是实际输出的**数据记录**数（不含控制对象）；JSONL footer 的 `dropped` 恒在
  （`0` 表示未截断），是本次运行因 `--trace-limit` 最终未输出的数据记录数；文本模式仅在
  截断时于 end 行追加 `dropped=<n>`。
- `status` 取 `ok`（正常终止）、`trap`（TC trap 终止）、`error`（工具或实现错误）；RXX 终结的旧段不新增 status 值，见 §6.7；
  文本模式的 `# tc-trace end <records> <status>` 使用同一词汇；发生截断时为
  `# tc-trace end <records> <status> dropped=<n>`。
- 只要 sink 仍可写，`error` 也必须写出 footer/end 行后再按 §5.1 的退出码结束；若 sink
  本身写失败，不输出 footer/end 行，直接按 §5.1 退出 70。
- `source` 与 §5.10 一致，为 root 模块的工程根相对路径。
- 确定性：`seq` 单调；`span` 使用工程根相对路径；同一程序重复执行产生相同字节。

### 5.5 `--stats`

- `tc-vm --stats`：运行时统计——执行指令总数、按 op 计数、最大抽象深度、五区使用峰值、
  分配次数、I/O 字节数；程序终止后输出。`executed` 是已提交执行的 IR 指令数，trap 指令
  本身计入、trap 后的指令不计入；`allocations` 是成功堆分配次数，`io_bytes` 是成功提交的
  `read`/`write`/`writeln` 字节数之和；`read` 按**逻辑消费**的输入字节计数（跳过前导空白
  + token 本身 + 终止分隔符；EOF 无分隔符；宿主预读不计数），失败/EOF 的 `read` 不增加
  `io_bytes`；`trace` 的 `io.bytes` 同口径，VM/AOT 运行时使用同一输入语义。
  `region_peak.*` 是各区最大使用字节数，`depth_max` 是最大抽象深度。trap 终止同样输出，
  并在固定字段末（`io_bytes` 之后）、按 op 计数之前追加
  `first_trap=<TC_RE_*>`（与 `--trace=trap` 的 `code` 同源）。
- `tc-aot --stats`：静态统计，分两级：
  - IR 派生（任何含 IR 最终化的模式，`--static-only` 也可用）：函数数、IR 指令数、
    按 op 计数、静态对象字节；
  - 目标派生（仅在内部代码生成/发射完成时）：最大帧大小、代码区需求；在内部发射后、
    外部工具链调用前输出。外部 clang/lld 缺失导致外部工具链失败（退出 70）时目标派生统计
    仍已输出；内部发射失败（退出 71）或发射 OOM（退出 70）时不输出目标派生字段。
  `--static-only` 下只输出 IR 派生字段，不输出目标派生字段。
- 输出到 `stderr`（`--stats-out=<path>` 可改）；首行 `# tc-stats 1 <tool> <profile>`，
  其后每行 `key=value`，字段顺序固定、只增不改义，供脚本解析。
- `tc-vm` 固定字段按顺序：`executed`、`depth_max`、`region_peak.frame`、
  `region_peak.static`、`region_peak.device`、`region_peak.heap`、`region_peak.code`、
  `allocations`、`io_bytes`；trap 终止时在 `io_bytes` 之后追加 `first_trap`。
- `tc-aot` 固定字段按顺序：`funcs`、`ir_instrs`、`static_bytes`；`funcs` 不含顶层单元
  （调试显示 token `<top>`）。发生内部代码生成时追加 `frame_max`、`code_bytes`；`--static-only` 不输出后两者。
- 两个工具都在固定字段之后输出按 op 计数，键为 `op.<完整打印名>`（与 trace 的 `op` 一致，
  含 `.wrap`/`.ieee`/`.volatile`/`.nsw`/`.nuw` 后缀），按完整打印名字节序排序；无出现的
  op 不输出。

### 5.6 trap 报告

`tc-vm` 的 trap 文本行保留既有 `error[RT]` 前缀，并在末尾追加结构化后缀：

```
<file>:<line>:<col>: error[RT]: TC_RE_X: PrintName [func=<top|name> bb=<n> insn=<n> op=<op> depth=<n> kind=<unit|function> module=<path>]
```

- 前缀（`file:line:col`、`error[RT]`、码、打印名）与 0.0.46 一致，脚本按首个
  `TC_RE_*` 匹配不受影响；后缀字段顺序固定、只增不改义。
- 实体显示名、`kind` 与 `module` 按 §5.4：顶层单元 `func=<top>`、`kind=unit`；函数
  `func=<源码名>`、`kind=function`；跨模块同名由 `module` 区分，不拼接 `#func_id`。
  `func`/`kind`/`module` 分别为 `<top>`/`unit`/root 模块显示路径与
  `top`/`function`/函数显示模块路径时可直接区分。`op` 是完整打印名（含 `.wrap`/`.ieee`/`.volatile`/`.nsw`/`.nuw` 后缀，与 trace
  一致）；`span` 取触发 trap 的指令；路径按 §5.9 根相对。
- trap 文本是面向人的单行格式；`module` 可含空格、`]`、逗号等合法显示路径字符，
  这些情况下文本后缀有歧义，机器解析必须使用 `--diag-format=json`。
- `--diag-format=json` 下 trap 输出一行 JSON：基础字段（§5.7）之后按旧顺序追加 `func`、
  `bb`、`insn`、`op`、`depth`，再追加 `kind` 与 `module`；`stage` 为 `RT`，`code` 为
  `TC_RE_*`。旧字段相对顺序不变，`kind`/`module` 是追加字段。

### 5.7 诊断机器接口

- `--diag-format=text|json`（默认 `text`）。文本形态固定为
  `file:line:col: error[<stage>]: <CODE>: <PrintName>`，其后可跟 note 行（见下）。
- `json` 为 JSONL，字段与文本一一对应，不含自由文本 `message`：

```json
{"code":"TC_CE_SYNTAX","severity":"error","stage":"SYN","print_name":"SyntaxError","file":"demo.tc","line":3,"col":5}
```

- `stage` 取 `LEX`、`SYN`、`IMPORT`、`SEM`、`CT`、`RT`；`col` 为 1-based Unicode 标量计数
  （标准 §12.5.1）。
- note 行同样输出为 JSON 对象，顺序与文本一致：
  - 相关位置：`{"severity":"note","note":"related position","file":...,"line":...,"col":...}`；
  - LEX 无效字节：`{"severity":"note","note":"invalid byte offset","file":...,"line":...,"col":...,"offset":N}`
    （`offset` 为 0-based 首个非法字节；位置字段与文本 note 行一致）。
- 一次运行至多一条 `error` 行：标准只报告首个诊断；note 行紧随其后，不构成第二条诊断。
- 静态诊断与 RT trap 都使用该格式；trap 行另带 §5.6 的追加字段。
- 文本与 JSON 必须同序、同码、同位置；顺序由标准 §12.2 首诊总序决定，不由输出格式决定。
- 文本与 JSON 的文件名都按 §5.9 根相对；两种格式使用同一路径值。
- 实现层错误不是语言诊断，不进入 JSON：始终以文本行
  `tool: <impl-error>: message[: detail]` 输出；`<impl-error>` 取 `tc_error_usage`、
  `tc_error_io`、`tc_error_unavailable`、`tc_error_internal`，内存耗尽取标准 §12.5.3 的
  `TC_ERR_OUT_OF_MEMORY`。`detail` 存在时同样只含工程根相对源路径（§5.9），不得含绝对
  路径；退出码按 §5.1。
- `json` 模式不输出颜色与人类说明文字。
- 字段只增不减：同一格式版本内新增字段必须追加，删除或改义需提升格式版本。

### 5.8 源级映射（AOT）

- `tc-aot --debug` 生成带调试信息的汇编：按源文件首次出现顺序分配确定性编号，发射
  `.file <fileno> "<显示路径>"`，并在每组 IR 指令之前发射
  `.loc <fileno> <line> <column>`；`.loc` 的第一个操作数是**文件编号**，不是路径。
  同时新增 `# source: <file>:<line>` 注释行（当前后端汇编不含源码注释，`--debug` 下为
  新增输出；非 `--debug` 时不发射），便于无 DWARF 场景对照。路径按 §5.9 根相对。
- `.loc` 的 `column` 直接取 §5.9 显示 span 的 1-based Unicode 标量计数，不做字节列
  转换；gdb 的列显示按该数值，行表验收以 `line` 为准、`column` 只作提示。
- `.file` 与 `.asciz` 路径按汇编字符串规则转义（引号/反斜杠）；`# source:` 注释使用同一
  根相对路径值，不含换行（路径含 Cc/Cf/Zl/Zp 在 §5.9 已被拒绝）。`--keep-temps` 保留的
  中间 `.s` 遵循同一规则：`--debug` 时含 `.file`/`.loc`，否则不含。
- 一条 IR 指令 lowering 成多条目标指令时，这些目标指令共享同一个 `.loc`；
  映射粒度是 IR 指令，不是目标指令。
- 汇编仍经 clang 集成汇编；`--emit=obj|elf` 时保留行表；`--emit=asm` 时 `.loc` 可见。
- `--emit=flat` 不承载调试段：`--debug` 仍生成带 `.loc` 的中间汇编与行表，最终 flat
  不含调试信息，此为文档化行为而非错误。
- 覆盖要求：行表产物（linemap）覆盖 IR 全序的每条指令；同一函数内 `.loc` 地址严格递增。
  行表是静态集合，`--trace=ir` 是动态执行序列（循环会重复、死代码不出现），两者不比较
  条目数；验收方式是对 trace 的每条记录按 `(kind,module,func,bb,insn)` 查行表（多对一），
  断言其 `span` 与记录 `span` 相同。`kind`/`module`/`func` 使用 §5.4 的单元/函数显示命名；
  trace 记录本身由 `seq` 唯一。
- 汇编器可能把相邻且 `(file,line,column)` 相同的 `.loc` 合并为一条 DWARF 行表记录，因此
  `.debug_line` 的记录数可以少于 IR 指令数；覆盖验收以 linemap 为准，不按
  `.debug_line` 行数计数。
- 调用 clang 汇编/链接时固定 `-fdebug-compilation-dir=.`，避免 `DW_AT_comp_dir` 泄漏
  绝对构建目录；gdb 从工程根启动，按根相对路径找源码。
- 行表与 trace 要求每条指令的 `span.file` 非空且已根相对化；驱动层的 `<source>` 只是
  诊断打印的防御回退，行表生成遇到空 `file` 视为实现错误（退出 71）。
- AOT 产物内的源文件路径字符串（`.asciz`）使用 §5.9 显示路径并按汇编字符串规则转义；
  代码段、数据语义与 ABI 不变；两个不同根目录构建同一工程时 asm/ELF/flat 必须逐字节一致。
  0.0.47 不承诺与 0.0.46 的绝对路径产物字节一致。
- 验证工具：`llvm-dwarfdump --debug-line` 或 `readelf --debug-dump=decodedline`；
  DWARF 版本随工具链，版本号记录在测试说明中。
- qemu gdbstub 冒烟：以 `qemu-system-* -s -S` 加载 AOT ELF，gdb 连接后按源码行下断点
  并单步，确认行表被真实调试器消费。

### 5.9 工程根与路径

- 根、manifest、布局、`tc` 工具与测试约定见 [TC 工程模型 0.0.47](TC工程模型-0.0.47.md)。
- 编译器从 root 模块 canonical 路径所在目录开始向上逐级搜索 `tc.project`。搜索以目录项
  存在为准：`lstat` 返回 ENOENT/ENOTDIR 时继续向上；返回其它错误（如 EACCES/ELOOP/EIO）
  → 70。存在 `tc.project` 条目的最近目录即工程根；读取/IO 失败 70、解析/字段/路径错误 2、
  超过 4 MiB 71，不跳过它继续向外层工程回退。工程根 = 包含该目录项的目录的 canonical
  路径；`tc.project` 是 symlink 时只跟随其内容，不改变工程根。`tc.project` 解析后必须是
  普通文件（拒绝目录、FIFO、设备等），否则 70。找不到 manifest 时根 = root 模块所在目录；
  root 模块不存在时用其父目录的 canonical 路径（父目录也不存在时，取最近存在祖先的
  canonical 路径并拼接剩余组件）作为根发现起点，该情况仍按源读取错误退出 70。
  `tc --test` 的 cwd 先 canonical 化，再按同一规则向上找 manifest；无 manifest → 2。
- 内部 canonical 路径用于模块实例键、去重、import 解析与显示路径求相对；显示路径以
  canonical 工程根为基准对 canonical 文件目标求相对；源文件不存在时用最近存在祖先的
  canonical 路径拼接剩余词法组件；所有输出使用工程根相对显示路径，symlink 书写形式不进入
  显示。
- root 模块必须是普通文件；目录、FIFO、设备、socket 或 dangling symlink → 70，不以
  可能阻塞的方式打开。import 搜索中的非普通候选 `m.tc` 视为不匹配，按标准 §4.5/§4.11.2
  继续下一个 include 根；全部根无匹配 → `TC_CE_IMPORT_NOT_FOUND`。仅对最终解析出的
  普通文件执行 `open`+`fstat` 复核；单源文件读取上限 4 MiB，超出按文档化实现上限 71。
  实现以 `open`+`fstat` 或等价方式校验类型，不把 `lstat`-then-`open` 作为唯一防线。
- 根外文件用 `../…` 相对路径，并在行表标 `"external":true`；不同卷/不可表示回退
  `./<basename>#<n>`。回退编号只对需要回退的文件分配：首次进入显示路径时从 1 递增，同一
  编译内永不复用或重排，分配后诊断、linemap、trace 使用同一编号。`<basename>` 取 canonical
  路径最后一段；不同编译模式因已发现文件集不同，编号可以不同。回退项同样标
  `"external":true`，部分检查模式同样适用。`./` 是回退命名空间保留前缀：正常显示路径由
  canonical 相对路径归一化后不以 `./` 开头；因此回退名不会与普通根内/根外路径碰撞。
- 不存在的 include 根是合法空候选根：用最近存在祖先的 canonical 路径拼接剩余词法组件做
  显示/匹配起点；import 搜索跳过（与无匹配候选相同），不报路径错误；全部根无匹配才报
  `TC_CE_IMPORT_NOT_FOUND`；只有路径字符/语法非法才按来源退出 2。
- 显示路径中出现的源路径与 include 根组件必须是合法 UTF-8 且不含 Unicode Cc/Cf/Zl/Zp。
  失败时按来源退出：CLI 参数或 manifest 值 → 2；缺省工程名与测试目录名按工程模型 §5/§6.1
  为 2；仅由文件系统枚举发现的其它路径 → 70。文本诊断、JSON、`# source:` 与汇编字符串
  依赖该约束；引号与反斜杠仍按各格式规则转义。
- import 名是 ASCII 标识符（标准 §2.3、§4.11.2），其路径组件只有 `<ident>.tc`；本规则
  的“用户提供路径”只覆盖 CLI 参数与 manifest 值，include 根与导入者目录链按各自的来源
  （CLI/manifest 或文件系统发现）处理。
- 路径消费者：文本诊断、诊断 JSON、实现/IO 错误源路径、trap 文本/JSON、`--dump-ir` 的
  `modules:`、trace `span`/header 与 `module`/`callee_module`、linemap、调试 ELF、AOT
  `.file`/`.loc`/`# source:`/`.asciz`、板端 trap、`monitor funcs`/`frames`/`trap`/`vreg`
  的 `module` 选择器、`tc --test` 失败诊断/侧车与布局错误路径、`tc --init-project`
  输出/错误；同一编译内所有输出（含部分检查）对同一文件使用同一路径；不同编译模式因
  已发现文件集不同，回退 `./<basename>#<n>` 可以不同。`tc --test` 报告中的 `<test>` 是逻辑
  `tests/<目录名>`，不是源文件 display 路径。
- `tc --test` worker 的 fork/pipe/临时捕获路径、PID 与实际耗时不属于路径消费者；报告
  只用测试名与根相对测试路径。
- `.file`/`.loc` 用显示路径；clang 调用固定 `-fdebug-compilation-dir=.`；gdb 从工程根
  启动；不写绝对 `DW_AT_comp_dir`。
- 0.0.47 不提供 `--debug-path-map`/`--trace-paths=abs` 一类绝对路径/前缀开关；
  clang/lld 自身诊断中的绝对临时路径属于外部工具输出，不纳入 TC 路径契约。
- 跨根构建 asm/ELF/flat 逐字节一致；代码/ABI 语义不变；不承诺与 0.0.46 的绝对路径
  产物字节一致。

### 5.10 共享行表产物

- 行表由单一模块生成（IR 指令序号 ↔ 源 span），供四处消费：AOT `.loc`、trace 记录、
  VM 调试 ELF（§6.8）、`--dump-linemap`。
- `--dump-linemap[=<path>]` 导出确定性 JSON，默认写 `stderr`：

```json
{"format":"tc-linemap","version":3,"profile":"aarch64-linux-user","source":"src/main.tc",
 "entries":[{"kind":"function","module":"src/main.tc","func":"main","bb":0,"insn":0,"file":"src/main.tc","line":3,"col":5}]}
```

- 条目顺序为 IR 最终化的全序；`kind`/`module`/`func` 使用 §5.4 的调试显示命名：顶层单元
  `kind="unit"`、`module` 为 root 模块显示路径、`func="<top>"`；函数 `kind="function"`、
  `module` 为模块显示路径、`func` 为源码名。条目的键顺序固定为 `kind`、`module`、`func`、`bb`、
  `insn`、`file`、`line`、`col`；`(kind,module,func,bb,insn)` 是 linemap 条目的唯一主键。
  仅 VM 上下文追加 `vm_addr`，仅根外条目追加 `external`，二者按此顺序出现在 `col` 之后，
  缺省键不占位。
- `vm_addr` 是 §6.2 合成地址的十进制 JSON 整数；`tc-vm --dump-linemap`（含
  `--static-only`）输出该键，`tc-aot --dump-linemap` 不输出。
- 根外文件（§5.9）的条目带 `"external":true`；根内条目不含该字段。
- 顶层 `source` 是 root 模块的根相对路径；多模块程序中条目各自带 `file`，`source`
  不随模块变化。
- 示例中的折行仅为排版；实际输出为单行紧凑 JSON，golden 按单行逐字节比较。

### 5.11 输出格式版本策略

| 格式 | 版本标识 | 兼容规则 |
| ---- | ---- | ---- |
| trace 文本 / JSONL | 文本首行 `# tc-trace 3`；JSONL 首对象 `header.version` | 版本内只追加字段；改义需升版本 |
| IR dump 文本 | 首行 `tc-ir 0.0.46 profile=…` | 语言版本随标准；字段只增不减，路径按 §5.9 根相对 |
| 诊断 JSON | `error` 行以 `code`、`note` 行以 `note` 为版本锚 | 版本内只追加字段 |
| 行表 JSON | `"version":3` | 版本内只追加字段；条目顺序为全序 |
| 调试 ELF | 无 TC 格式版本字段；DWARF v4 固定（§6.8） | 只承载符号与行表；不承诺向后兼容 |
| GDB RSP | 协议版本由 `qSupported` 协商 | 子集只增不减；废弃报文先返回空应答再移除 |
| `--stats` 文本 | 首行 `# tc-stats 1` | 字段只增不减；字段名、含义与顺序见 §5.5 |
| trap 报告文本 | 沿用诊断文本格式 | 字段顺序固定、只增不改义；不单独版本化 |

### 5.12 `tc-reduce` 契约

- 用途：把触发某个诊断或 trap 的源程序缩到最小复现，服务于缺陷定位与回归语料。
- 调用：`tc-reduce <source.tc> [--timeout=<seconds>] [--stdin=<path>] [--output=<path>] --
  <tool> <args...>`；工具参数中的 `{}` 表示候选源文件，可出现多次并替换为同一路径；
  不含 `{}` 时追加到参数末尾。首次运行记录"失败指纹"：首个 `TC_CE_*`/`TC_RE_*`、退出
  状态与终止方式；缩减过程中指纹必须保持不变。
- `--timeout`、`--stdin`、`--output` 各至多一次；`--stdin`/`--output` 只接受
  `--opt=<path>` 形式，`--timeout` 只接受 `--timeout=<seconds>` 形式；空值或重复 → 2。
- `<source.tc>` 必须是第一个参数；缺少 `<source.tc>`、`--` 或 `<tool>`，或 `--` 前出现
  未知/重复选项或多余位置参数 → 2；`--` 后的参数全部作为 `<tool>` 及其参数，不再解析
  `tc-reduce` 选项。
- 候选位置：种子运行使用原始 `<source.tc>`；后续候选写入其 canonical 目录下的
  `tc_reduce_<n>.tc`（合法标识符、`.tc` 扩展名、`O_EXCL` 创建）。`n` 从 1 开始递增，
  取第一个不与缩减器递归闭包中的 import 名（含未解析成功的 import 名）、已有条目
  （文件或目录）、`--stdin` 或 `--output` 的解析路径冲突的值；import 名与候选 basename
  去 `.tc` 后比较，路径比较前按 §5.9 规则 canonical 化（不存在路径用最近存在祖先拼接）。
  递归解析使用与编译器相同的导入者目录优先、include 根按序规则；缩减动作只删除
  import，种子闭包覆盖候选闭包，因此检查种子闭包足够。同目录保证 `tc.project` 根发现、
  manifest include 与相对 import 解析不变；不得写入工程外临时目录，缩减过程中不得修改
  原文件（`--output` 等于源文件的显式破坏性写入除外，见下条）。无法创建/写入候选 →
  70。候选文件在每轮结束及退出时删除；其路径/文件名不进入指纹、结果或诊断。
- 结果交付：默认把当前最佳源文本按原字节写 stdout；进度/诊断写 stderr，stdout 不含其它
  内容。`--output=<path>` 显式指定结果文件（临时文件 + rename 原子写，写失败 70），指定
  后 stdout 不写结果。0.0.47 不隐式就地覆盖 `<source.tc>`；`--output` 等于源文件时按
  显式破坏性写入处理。不要用 stdout 重定向到输入文件。
- 缩减动作：删除整条语句/声明/未使用 `import`、简化字面量、删除不可达函数；
  每步之后重新解析，保持源文件可解析。
- 被测程序的 `stdin` 由 `--stdin=<path>` 提供；该路径解析后必须是普通文件（FIFO/设备等
  → 70）；每次重跑（种子与每个候选）都重新打开该文件并从头读取；缺省为空输入（每次
  立即 EOF）；trap 复现同样适用。
- 工程内运行时继承 `tc.project` 的根/profile/include；manifest 读取沿用工程模型 §3.1
  （含 4 MiB 大小上限）；诊断路径按 §5.9 根相对；指纹仍按首个 code/exit/终止方式，不按路径。
- 保证：当前最佳结果初始为输入；每轮成功缩减严格减少行数或字节数；若不存在可减少的
  候选，则以当前结果结束（可能等于输入），退出 0。不启用 `--timeout` 且被测工具确定性时
  同输入同输出；启用后到期与否取决于运行时序（§5.1），缩减结果不参与逐字节确定性门禁。
- 退出码按 §5.1：正常收敛退出 0；种子首次运行未触发任何诊断或 trap（无指纹可记）为
  用法错误，退出 2。`--timeout=<seconds>` 是**整个缩减过程**的 deadline：从首次调用被测
  工具开始计时，覆盖种子运行、候选生成与全部重跑；`<seconds>` 只含 ASCII 十进制数字、
  非负、不接收符号/空格/小数，溢出 → 2；缺省或 `0` 表示不限。被测工具放入独立进程组/会话，
  到期后终止并回收整组（含后代、管道持有者）；候选生成阶段同样受该 deadline 约束。到期
  按结果交付输出当前最佳结果并以退出 1 结束；种子运行超时同样输出输入（当前最佳）并按 1
  结束，不按无指纹的 2；清理/终止失败不改变交付与退出码。任何情况下结果写 stdout 或 `--output` 失败 → 70。
- 被测工具返回 70/71 或无法启动时，`tc-reduce` 以相同退出码结束：已建立指纹时按结果
  交付输出当前最佳结果；种子运行返回 70/71 或无法启动、尚未建立指纹时不输出结果。这类
  失败不建立指纹，也不作为可接受候选。`--stdin` 路径读取/打开失败 → 70。
- `tc-reduce --timeout` 只约束缩减过程，不是 `tc --test --test-timeout` 的别名；两个
  开关互不适用，用于另一工具时按 §5.1 退出 2。
- 非目标：不做语义等价化、不改 include 根、不合成新程序。

## 6. VM 调试器（GDB RSP）

### 6.1 会话模型

- 开关：`--gdb[=<port>]`，缺省端口 0（由系统分配）。端口只接受 `--gdb=<port>` 形式，
  `<port>` 只含 ASCII 十进制数字、取值 0–65535；空值、符号/空格、越界或溢出 → 2。
  只绑定 `127.0.0.1`，不提供远程监听；端口不是独立位置参数。
- 启动后 stub 在第一条指令执行前阻塞等待调试器连接；未连接时程序不推进，Ctrl-C 终止。
  连接成功后按调试器命令推进程序。
- 会话信息打印到 `stderr`：`tc-vm: gdbstub on 127.0.0.1:<port>`；该行含动态分配端口，
  属会话信息，不属于第 1 节确定性约束的逐字节比较对象。它固定为**运行时输出的首行**：
  在编译期诊断/`--dump-ir`/`--dump-linemap` 之后、trace 记录流之前；编译失败未进入 VM
  时不打印。它不属于 trace、诊断或统计流；与 `--trace-format=jsonl` 或
  `--diag-format=json` 同用时，解析器必须跳过该行，或用 `--trace-out` 等显式路径分流。
- 端口被占用或绑定失败：环境不可用，退出 70；`--gdb` 参数非法或对 `tc-aot` 使用：退出 2。
- 调试器经 TCP 通信，不占用程序 `stdin`；程序仍按标准读取自己的输入，`stdout` 不变。
- 工程内运行时，`monitor` 与调试 ELF 的源码路径按 §5.9 根相对；gdb 从工程根启动。
- 调试器意外断开或校验和连续三次失败：程序停在当前指令边界，stub 关闭该连接并回到
  监听；重新连接后 `?` 按 §6.3 的状态报告：暂停中为 `T05thread:1;`，已终止为 `W<hh>`
  （自然终止与 trap 终止一致）。
  进程不退出。
- RSP 是开发接口，不属于语言语义，也不进入差分门禁。

### 6.2 合成代码地址空间

VM 执行的是扁平 IR，没有目标机器码；为支持地址级断点与 `pc`，定义不参与语言值域的合成
代码地址：

```
W=64: TC_DEBUG_CODE_BASE = 0x5443_0000_0000
W=32: TC_DEBUG_CODE_BASE = 0x5443_0000
addr(insn) = TC_DEBUG_CODE_BASE + 4 * global_insn_index
```

- `global_insn_index` 是 IR 最终化后的全局单调指令序号，跨函数连续，确定且可复现。
- 合成代码区间是 `[TC_DEBUG_CODE_BASE, TC_DEBUG_CODE_BASE + 4 * N)`，`N` 为 IR 最终化后的
  全局指令数；有效指令地址恰为 `TC_DEBUG_CODE_BASE + 4 * i`（`0 <= i < N`）。`Z0`、
  `P pc`、`monitor ir` 的地址必须是有效指令地址，否则 `E01`；`monitor ir` 的地址字面格式见 §6.6；`m` 允许区间内任意字节
  地址，越界或跨出区间边界 → `E01`。
- 该地址区间不属于任何平台档案的五个区，不进入 `funcaddr`/`bitcast` 结果，不参与程序行为；
  它是当前档案 W 的调试视图，不是目标地址：W=64 使用 64 位区间，W=32 使用 32 位区间，
  区间必须落在对应地址空间内。W=32 的程序可见地址仍按 32 位呈现。
- `--gdb` 会话启动与 `--gdb-elf-out` 生成时校验合成区间与当前档案区表不重叠；重叠则
  实现错误退出 71，不得退化为歧义地址。
- `m` 对合成区间返回全零；断点由 stub 处理（`Z0`），不写内存。

### 6.3 报文子集（v1）

| 类别 | 报文 | 行为 |
| ---- | ---- | ---- |
| 握手 | `qSupported`、`QStartNoAckMode`、`!` | 报告 `PacketSize=4000;vContSupported+;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+`（`4000` 为十六进制 0x4000=16384 个 data 字符）；支持免应答模式；`!` 启用扩展模式并回 `OK`，`RXX` 仅在扩展模式可用 |
| 停止原因 | `?` | `T05thread:1;`（断点/单步/暂停），`T02thread:1;`（中断）；终止态（自然终止或 trap 终止）一律 `W<hh>`，trap 详情经 `monitor trap` 读取 |
| 中断 | `\x03`（带外字节，不是 `$` 包） | 运行中收到时完成当前指令并停在下一条边界，报告 `T02thread:1;`；未运行时忽略 |
| 寄存器 | `g`、`p<n>` | `g` 与 `p<n>` 均支持；`g` 按 regnum 顺序返回 4 个寄存器的十六进制串（共 64 个十六进制字符），`p<n>` 仅接受 `n=0..3`，其他编号返回 `E01`；编码见 §6.4。`P` 默认返回 `E01`，`--gdb-allow-write` 下仅允许写 `pc`，且必须落在合成代码区间的指令边界，否则 `E01` |
| 内存 | `m`、`M`、`X` | 读五区与合成代码区；写默认 `E01`，`--gdb-allow-write` 下仅限帧/静态/堆区；跨区边界或区外的 `M` 返回 `E01`；`X` 不支持，返回空应答 |
| 执行 | `s`、`S`、`c`、`C`、`vCont;s`、`vCont;c`、`vCont?` | 单步、继续；单步粒度为一条 IR 指令；`S`/`C` 的信号参数忽略；执行命中 TC trap 的停止应答为 `T0bthread:1;`；`vCont?` 返回 `vCont;s;c` |
| 断点 | `Z0`、`z0` | 指令边界断点，最多 64 个唯一地址；同址重复 `Z0` 幂等（回 `OK`，不占新槽），`z0` 对未设置地址回 `OK`；超出或非法地址返回 `E01`；`Z1`–`Z4` 返回空 |
| 线程 | `qC`、`qfThreadInfo`、`qsThreadInfo`、`Hc`/`Hg`、`qAttached` | 单一合成线程 `1`；`qC` 返回 `QC1`；`qfThreadInfo` 首次返回 `m1`、再次返回 `l`；`qsThreadInfo` 返回 `l`；`Hg0`（any thread）、`Hc-1`（all threads）与 `Hc1`/`Hg1` 都等价于合成线程 1，返回 `OK`；其他 `Hc`/`Hg` 返回 `E01`；`qAttached` 返回 `0`（程序由 stub 创建）；`vCont;s`/`vCont;c` 接受可选 `:1` 后缀，其他线程号返回 `E01` |
| 特性 | `qXfer:features:read`、`qXfer:memory-map:read` | `target.xml`（4 个寄存器）与五区内存映射；`offset`/`length` 为十六进制；支持分块读取 `m`/`l` 语义。`PacketSize` 限制 packet-data 字符（不含 `$`/`#`/校验和），`m`/`l` 前缀计入；单次应答 data 字符 ≤ `PacketSize`，即有效数据 ≤ `PacketSize-2`；请求更大时返回可容纳前缀和末尾 `l` |
| 监视 | `qRcmd` | §6.6 的 `monitor` 命令；应答为 `O` 分块 + `OK`/`E01`，不以 `W` 应答 |
| 生命周期 | `D`、`RXX`、`k`、`vKill` | `D` 回 `OK`：stopped/running 态分离后程序继续；终止态以最近一次运行的退出状态退出；两种情况都先回 `OK` 再关闭连接。`RXX` 成功时无应答，复位到入口并保留断点；仅扩展模式下可用，未启用扩展模式时回 `E01`；本次运行的帧、trap 记录、trace/stats 段清空，I/O 语义见 §6.7。`k` 无应答：stopped/running 态不改变程序状态（v1 不支持调试器主动杀死），终止态关闭连接并退出。`vKill` 在 stopped/running 态回 `E01`，终止态回 `OK` 并退出 |
| 退出 | `W<hh>` | `W<hh>` 只在自然终止或终止态出现：自然终止的执行报文、终止态下的执行报文与终止态下的 `?` 回 `W%02x`（0–255）；命中 TC trap 的执行报文先回 `T0b`（§6.7），之后的 `?`/执行报文按终止态回 `W<hh>`。`W` 只作为执行报文与 `?` 的停止应答；越界状态属实现错误（退出 71） |

- 帧格式按协议：`$<payload>#<checksum>`；在免应答模式之前，每个报文都需 `+`/`-` 应答。
- `PacketSize=4000` 是 RSP 十六进制值（0x4000=16384 个 data 字符），不含帧和校验和；
  stub 按 data 字符数 `16384`（另留 NUL/内部余量）实现。`qXfer` 的 `offset`/`length` 为
  十六进制；应答 `m <data>`/`l <data>` 的前缀计入 `PacketSize`，有效 data ≤ `PacketSize-2`。
- 会话只有 running、stopped、terminated 三态。running（`c`/`s`/`vCont` 执行中）只处理
  带外 `\x03` 与本节的 `D`/`k`/`vKill`；其它报文（`qRcmd`、`m`、`Z0`、`g` 等）不解析、
  不应答。实现必须消费完整 `$...#xx` 帧并按校验和处理：校验正确但非生命周期报文直接丢弃，
  不排队、不在停止后补执行；校验错误按重发/重同步规则处理；遇到无 `$` 的垃圾字节丢弃并重新
  同步。running 队列有界且不得改变执行；客户端不得在 running 发送这些报文；qRcmd 的
  `O`/`OK` 不能插在停止应答之前。协议套件只断言 running 下的 `\x03`、生命周期子集与丢弃/
  重同步行为。
- `vCont` 只接受单动作 `s`/`c`（可带 `:1`）；多动作、其他动作或空动作返回空应答，已支持
  动作的其他线程号返回 `E01`。
- 未列出的报文一律返回空应答；不支持的写操作返回 `E01`，不得静默忽略。
- `RXX` 仅在 `!` 成功后可用；未启用扩展模式时收到 `RXX` 返回 `E01`；`XX` 为两个十六进制数字，内容忽略。
- 终止态下 `?` 回 `W<hh>`；`RXX` 复位后可再次运行，复位后的 `?` 回 `T05thread:1;`。
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
    <reg name="depth" bitsize="64" regnum="1" type="uint64"/>
    <reg name="sp"    bitsize="64" regnum="2" type="uint64"/>
    <reg name="insn"  bitsize="64" regnum="3" type="uint64"/>
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

- 寄存器位宽与编码和档案 W 无关：四个寄存器恒为 64 位，`g`/`p` 恒为 16 个十六进制位/寄存器。
  W=32 下 `pc` 的取值受 §6.2 的 32 位合成区间约束，但编码仍是 64 位，高 32 位为 0。
- `P pc`、`Z0`/`z0` 的地址字段同样固定为 64 位 RSP 编码；W=32 时高 32 位非 0 或超出
  32 位合成区间返回 `E01`。`m`/`M` 地址字段同理，W=32 时按 32 位有效地址零扩展为
  64 位 RSP 地址。

- `target.xml` 不声明 `<architecture>`；架构由调试 ELF（若提供）决定，缺省时 gdb 使用其
  默认架构。寄存器集完全由本描述定义。
- 不暴露逐个 vreg 寄存器：IR 没有目标机寄存器分配，暴露伪寄存器会误导调试者；
  vreg 通过 `monitor vreg` 读取。
- 自定义架构与寄存器集在 gdb 各版本的行为差异属于 §8 风险，由 §8.1 的 spike 验证。

### 6.5 内存视图

- `qXfer:memory-map:read` 返回五区（帧/静态/设备/堆/代码）基址与长度，来自当前平台档案。
  结构固定为以下顺序；`start`/`length` 是档案的十六进制地址值，`region` 属性用于区名标识，
  合成代码区不出现：

```xml
<?xml version="1.0"?>
<memory-map>
  <memory type="ram" start="0x..." length="0x..."><property name="region">frame</property></memory>
  <memory type="ram" start="0x..." length="0x..."><property name="region">static</property></memory>
  <memory type="ram" start="0x..." length="0x..."><property name="region">device</property></memory>
  <memory type="ram" start="0x..." length="0x..."><property name="region">heap</property></memory>
  <memory type="ram" start="0x..." length="0x..."><property name="region">code</property></memory>
</memory-map>
```

- `m` 读取不触发程序语义：直接读取区镜像，不消费设备输入、不改变指针状态、不产生 trap；
  五区与合成代码区之外的地址返回 `E01`；跨出所在区边界的整次读取同样返回 `E01`，
  不做部分读取。
- 代码区在 VM 中只承载函数地址语义，没有指令字节；读取返回全零，函数定位用
  `monitor funcs`。
- 合成代码区（§6.2）不在内存映射中：它不是程序可访问区，断点由 stub 直接处理。
- 设备区默认可读但不可写；`--gdb-allow-write` 也不放开设备区写。
- 写帧/静态/堆区可能破坏语言不变量；checked build 在后续访问处按既有规则 trap，
  调试器会话不因此改变必选 trap 集合。

### 6.6 监视命令

`qRcmd` 子集，输出为十六进制编码文本：

| 命令 | 内容 |
| ---- | ---- |
| `monitor regions` | 五区基址/长度/当前使用量（字节） |
| `monitor funcs` | 函数表：`module`（根相对显示模块路径）、源码函数名、`funcaddr` 值（按区名+区内偏移）、指令区间（全局指令序号）、合成地址范围；顶层单元不是函数，不列入 |
| `monitor objects` | 对象表：对象 id、区、偏移（字节）、大小（字节）、生命期状态；对象 id 是内部标识，不承诺跨构建稳定 |
| `monitor frames` | 抽象帧栈：深度、实体 `kind`（`unit`/`function`）、`module`、`func`（顶层 `<top>`，函数源码名）、全局指令序号（§6.2）、源码位置 |
| `monitor next` | 执行到下一条同深度指令，跨越整个 `call`/`call_indirect`；途中命中用户断点、TC trap、终止或 `\x03` 时提前停止 |
| `monitor finish` | 运行到当前帧返回（深度减一）；途中命中用户断点、TC trap、终止或 `\x03` 时提前停止 |
| `monitor ir [<addr>]` | 指定地址或当前 pc 的 IR 文本（含操作数与 span；可能含 `%N`/`f<id>`/`struct#id`/label 等结构操作数，不是调试实体身份）；`<addr>` 必须是 `0x` 前缀的 1–16 位 ASCII 十六进制字面量（大小写数字均可，无符号/空白/下划线；不接受 `0X`、裸十六进制或十进制），按 64 位无符号解析；W=32 时高 32 位必须为 0 且落在 32 位合成区间；地址越界、未对齐、不对应指令或字面非法返回 `E01`；省略时使用当前 pc |
| `monitor vreg [<selector>:]<序号>` | 读取当前帧的 vreg 值：标量按 vreg 自身类型的位宽打印为 `0x` 十六进制位模式，宽度取 `tc_type_bits(type, W)`：`int8`/`uint8`/`bool` 2 个十六进制数字、`int16`/`uint16` 4 个、`int32`/`uint32`/`float32` 8 个、`int64`/`uint64`/`float64` 16 个，`isize`/`usize` 随档案字长（W=32 为 8 个、W=64 为 16 个）；指针/fnptr 打印为区名+区内偏移（空指针为 `null`，不可表示时按 W 位模式）；聚合与未初始化 vreg 返回 `E01`。`<序号>` 为 vreg 序号的十进制字符串，即 `monitor ir` 显示的 `%N`（IR 操作数不携带源名）；省略选择器时在当前实体的 vreg 空间内解析，未找到返回 `E01` |
| `monitor stats` | §5.5 的 `tc-vm` 运行时字段，每行 `key=value`，不含 `# tc-stats` 头 |
| `monitor trap` | 最近一次 TC trap 的码、span、`kind`、`module`、`func`、`bb`、`insn`、`op`、`depth` |
| `monitor help` | 命令列表 |

- vreg 只存在于当前帧：暂停中的活动帧，或 trap 终止时保留的触发帧。跨实体同序号必须写全
  选择器：顶层单元 `monitor vreg <top>:<序号>`；函数 `monitor vreg <module>:<func>:<序号>`。
  选择器是 `vreg` 之后的原始剩余串（qRcmd 解码后），**允许空格**；stub 不按空白切分
  selector，宿主 shell 是否引用不影响协议。选择器必须等于当前帧实体，指向其他实体返回
  `E01`，不支持读取调用者帧。函数选择器以**最后一个** `:` 分隔 `<序号>`，再以**倒数
  第二个** `:` 分隔 `<module>` 与 `<func>`；函数名不含 `:`，模块显示路径可以含 `:`，
  因此从右解析安全。`<top>` 是字面 token；在 shell 中传递时需引用。`<序号>` 为非负十进制
  整数，溢出或未知序号返回 `E01`。
- `monitor funcs` 只列函数；`const`/`static var`/`struct` 不是函数，不进入函数表。若未来增加
  成员级调试命令，使用 `(member_kind,module,name)`；结构体字段使用
  `(module,owner_struct,field_path)`；局部变量使用 `(module,function,scope,name)`。
- `monitor objects` 的对象 id 是内部标识，不承诺跨构建稳定；不要把它当作源码成员身份。
- `monitor finish` 在顶层帧（深度 0）返回 `E01`；`monitor next` 在当前指令是 `return`
  或 `trap` 时执行该终止指令并在调用者/终止态停下，不再寻找同深度指令。
- `monitor next`/`monitor finish` 与 `c` 一样在每个指令边界检查用户断点；进入命令时若
  当前 pc 处于已设置断点，先执行该指令一次（step-over），避免在同一地址立即停止。执行
  途中命中用户断点、TC trap、程序终止或 `\x03` 时提前结束：qRcmd 回 `OK`，停止状态由
  `?` 报 `T05`/`T02`/`W`，trap 详情用 `monitor trap`。qRcmd 的 `OK` 只表示本次执行请求
  结束，不保证到达目标；命令不保存未达目标，重新发起时从当前 pc/depth 重新计算。目标
  指令本身有断点时，到达目标即停止，后续 `c` 按 step-over 规则处理。
- `monitor trap` 在首次 trap 之前返回 `E01`。终止态下 `monitor ir` 无地址参数返回
  `E01`，`monitor regions`/`objects`/`funcs` 仍可读取终止快照；trap 终止保留触发帧，
  `monitor frames`/`monitor vreg` 按该帧读取；自然终止没有当前帧，二者返回 `E01`。
- 终止态下 `monitor next`/`monitor finish` 不执行新指令：qRcmd 以 `O` 文本说明程序已
  终止后回 `OK`，终止状态由 `?` 报 `W<hh>`；`W<hh>` 不得作为 qRcmd 的应答。
- `qRcmd` 的 command 为十六进制编码；输出按 `O<hex>` 分块，最后 `OK`；非十六进制或奇数
  长度 → `E01`。`monitor next`/`monitor finish` 的 qRcmd 应答只表示命令完成；停止后
  `?` 报当前状态（`T05` 暂停、`T02` 中断、`W<hh>` 终止），trap 详情用 `monitor trap`。
- 所有 `monitor` 命令只在 stopped 或 terminated 态执行；running 态按 §6.3 处理。
- 未知命令返回 `E01` 并附 `monitor help` 提示；命令解析失败不影响程序状态。
- 输出为纯文本，行尾 `\n`，不依赖终端宽度；源码位置与路径按 §5.9 根相对；同一状态重复查询结果一致。
- `monitor` 输出是面向人的视图。`module` 是显示路径，可含空格、`]`、逗号等合法字符，按
  空白或固定分隔符做通用行解析会产生歧义；monitor 不提供 JSON 通道。机器消费方必须使用
  已知测试夹具与固定字段顺序，不能把 monitor 文本当作稳定机器接口。

### 6.7 断点、单步与继续语义

- 断点地址必须是 §6.2 的有效指令地址（`BASE + 4i`，`0 <= i < N`）；越界、未对齐或不对应
  指令返回 `E01`。
- `s`：执行一条 IR 指令。`call`/`call_indirect` 在调用者 CFG 中是单条原子指令：当前停在 `call` 的前一条
  指令时，`s` 停在 `call`（尚未进入被调函数）；当前停在 `call` 时，`s` 执行该调用并停在
  被调函数首条指令（`depth` 加一）。
- `c`：运行到断点、TC trap、程序终止，或被 `\x03` 中断（停在下一条指令边界并报告
  `T02thread:1;`）。
- 断点命中后 pc 停在断点指令（尚未执行）。若该地址仍有断点，再次 `c` 时先静默执行该
  地址的一条 IR 指令（step-over），再从新 pc 正常检查断点，因此同一地址不会立即重触发；
  step-over 指令 trap 时按 `T0b` 停止，收到 `\x03` 时完成该指令后按 `T02` 停止。`s` 不做
  step-over：执行一条指令后停在下一条。
- 跨越调用用 `monitor next`（含直接 `call` 与间接 `call_indirect`），返回用 `monitor finish`；gdb 的 `next`/`finish` 依赖架构识别
  与栈展开，不作为验收项（§8）。
- TC trap 先完成 §12.5.2 的提交语义（trap 生效、已提交的 I/O 不回滚），再停止并报告
  `T0bthread:1;`；此时程序已进入终止态，`monitor trap` 可读取 trap 详情。T0b（SIGSEGV）
  是有意选择：TC trap 是程序故障，不用 T05 以免与断点/单步混淆；gdb 可能显示
  "Program received signal SIGSEGV"，属表现层，`?` 随后按终止态报 `W<hh>`。T0b 只作为
  执行报文（`c`/`s`/`C`/`S`/`vCont`）的直接停止应答；`monitor next`/`finish` 触发的 trap
  用 `?` + `monitor trap` 读取。
- 程序终止（自然终止或 trap 终止）后 stub 返回 `W<hh>`；退出状态取档案退出策略，
  与无调试器运行完全一致。终止态下 `s`/`c` 不执行新指令，直接再回 `W<hh>`；`RXX`
  可复位重跑。终止后进程保持存活以支持 `RXX`；`D` 关闭连接并按该退出状态退出，
  `k`/`vKill` 按 §6.3 处理；调试器意外断开时按 §6.1 等待重连，进程不退出。
- `RXX` 不重置 I/O：已提交的 `write`/`writeln` 不回滚（宿主逐条提交），`stdin` 为消费式
  读取、不回退；复位后的输出会再次提交，因此 `RXX` 是显式调试动作，不承诺可复现（§6.9）。
- `RXX` 清空本次运行的帧、trap 记录与统计/trace 状态；旧 trace 段不补 footer（保留已输出的
  truncated 标记，记为 abandoned segment），不新增 status 值；旧段未输出 header 时同样不补。
  复位再次运行视为新的一段：`--trace` 重新输出 header（`seq` 从 1 开始）、终止时输出 footer
  与 trap/stats；
  同一 `--trace-out`/`--stats-out` sink 继续写入、不截断已有字节，按 header 分段；
  `--gdb` 会话行只在进程启动时输出一次。
- 调试暂停不产生程序可观察效果：暂停期间不推进、不超时、不提交 I/O。
- 断点表的增删在 stopped 或 terminated 态立即生效；后续 `c` 与 `monitor next`/
  `monitor finish` 使用最新断点表，`s` 仍按单步语义执行一条指令。running 态的报文处理
  见 §6.3；命中已删除断点的陈旧地址不得触发停止。

### 6.8 与 AOT 调试视图的一致性

- AOT 与 VM 共用 §5.10 的行表生成器；同一源程序的 `file:line:col → 指令` 映射一致。
  调试 ELF 的 `.file` 使用 §5.9 显示路径；gdb 从工程根启动；不写绝对 `DW_AT_comp_dir`。
- VM 可选生成**调试 ELF**（`--gdb-elf-out=<path>`）：由 `tc-vm` 内部最小 ELF writer 直接
  生成，只承载符号与行表，地址使用合成代码地址，不含可执行代码；不调用 clang、lld、
  gdb、readelf 或其它外部工具。调试 ELF 的函数符号使用源码函数名（即 `func`），不追加
  `#<func_id>`；不同模块同名函数由 DWARF `file:line` 区分，`break <name>` 不保证唯一，
  源级断点用 `break <file>:<line>`。顶层单元不是函数符号，不写入 ELF。TC 调试身份元组
  `(module,func)` 不作为合成键写入 `.symtab`/DWARF，`kind`/`<top>` 不写入；模块显示路径
  通过 DWARF `decl_file`/行表体现。gdb 以 `file <elf>` + `target remote` 获得源级断点时，
  断点地址与 stub 的合成地址一致；单个 `top` 函数仍可用 `break top` 下源级断点。
- `tc-aot --debug --emit=elf` 的 ELF 符号仍是后端内部 `tc_main`/`tc_function_<func_id>`；
  AOT 的 gdb 调试按 DWARF `file:line`，不承诺 `break <源码函数名>`。本节源级符号命名只
  针对 VM `--gdb-elf-out` 调试 ELF。
- VM 调试 ELF 的 ELF class/machine/endianness 固定如下表；`e_type`、节区顺序、具体最小
  节集（`.symtab`/`.strtab`/`.shstrtab`/`.debug_line`，以及 gdb 所需的 DWARF v4 节）
  与 §6.2 合成段基址在 §8.1 spike 中确认。spike 只调整布局/节集，不改为外部
  assembler/linker；目标文件格式只服务于 gdb 加载符号，不用于执行、反汇编或分发。

| profile | ELF class | data | e_machine | DWARF |
| ---- | ---- | ---- | ---- | ---- |
| `aarch64-linux-user`、`aarch64-baremetal` | `ELFCLASS64` | `ELFDATA2LSB` | `EM_AARCH64` (183) | 4 |
| `arm32-baremetal` | `ELFCLASS32` | `ELFDATA2LSB` | `EM_ARM` (40) | 4 |
| `riscv32-baremetal` | `ELFCLASS32` | `ELFDATA2LSB` | `EM_RISCV` (243) | 4 |

- `w32` 是 target-independent VM static profile，没有具体 `e_machine`；`w32` 的
  `--gdb-elf-out` 退出 70（profile 能力不可用），不生成调试 ELF。`w32` 的 `--gdb` 仍可用
  `target.xml`（不声明 architecture，gdb 使用默认架构或显式 `set architecture`），但不提供
  源级符号 ELF。
- VM 调试 ELF 与 AOT `.loc` 共用 §5.10 的行表生成器映射，但**不**调用 AOT 的 clang/lld/
  assembler/linker 管线；两边的 `file:line → 指令` 映射一致由共享行表测试覆盖。
- `--gdb-elf-out` 可与 `--gdb` 同用，也可单独使用（生成 ELF 后正常执行），便于先产出
  调试 ELF 再启动会话；与 `--static-only` 同用时只生成 ELF、不执行（§5.2.1）。
- 调试 ELF 是可选增量，不阻塞 T3 验收；无调试 ELF 时 gdb 仍可按合成地址下断点。

### 6.9 安全与语义约束

- stub 默认只读程序状态；`--gdb-allow-write` 下可写 `pc` 与帧/静态/堆区（§6.5），
  其余写操作返回 `E01`。
- 被动会话（不写寄存器/内存、不复位）下，stub 不得改变 stdout 字节、trap 选择与退出状态；
  同一程序有/无调试器运行必须字节一致。
- 写 `pc`、写内存与 `RXX` 复位会真实改变执行结果，不承诺可复现：宿主 I/O 逐条提交
  （`write`/`writeln` 立即 fflush），`RXX` 重跑会再次提交已提交的输出；`stdin` 为消费式
  读取，不回退。这些是显式调试动作，结果由调试者负责。
- 写 `pc` 只改变下一条执行指令，不调整 `depth` 与帧栈；跳入函数体或跨帧跳转会破坏帧
  一致性，后续访问按既有规则 trap 或产生 UB，stub 不做一致性修复。
- stub 不得把宿主地址泄漏为目标观察值；程序可见地址按目标字长 W 呈现，合成调试地址
  按 §6.2 的当前档案 W 调试视图呈现（W=64/W=32 各自区间），两者不得混用。
- 单步与断点检查在必选 trap 之前完成；trap 本身仍按 §12.5.2 提交语义执行。
- 威胁模型：只服务本机开发者，绑定回环、无认证、单客户端；不支持远程调试，
  文档不得把它描述成可远程使用的调试服务。

### 6.10 测试策略

- 仓库内提供最小 RSP 客户端（Python，`tests/tools/vm_gdb_client.py`），覆盖：
  帧与校验和、免应答模式、分块 `qXfer`、断点命中地址、单步计数、`monitor next` 跨直接/间接调用、`monitor finish` 返回、`monitor next`/`finish` 入口断点 step-over、途中断点与目标处
  断点、命中后重发、运行中 `\x03` 中断（停在边界并报 `T02`）、寄存器读写（`P` 默认
  `E01`、allow-write 写 `pc`）、内存读写拒绝与区边界、断点命中、越界拒绝与删除后陈旧
  地址不触发、`!` 扩展模式、
  `vCont?`、`RXX` 无应答与复位重跑、`D` 回 `OK` 并分离后继续、`k`/`vKill` 应答、
  `Z1`–`Z4` 空应答、同址重复 `Z0`/`z0` 幂等、断点处连续 `c` 的 step-over、step-over 中
  `\x03`、合成区间越界/非指令地址拒绝、终止态 `monitor next` 的 `OK` + `?`=`W`、vreg
  类型宽度（W=32/64，含 W=32 的 int64/float64 与 W=64 的 float32）、各 `monitor` 命令、
  trap 停止码、退出状态、坏校验和重同步。
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
# 需要 restart（RXX）时改用 target extended-remote 127.0.0.1:53412
(gdb) break demo.tc:7
(gdb) continue
# 无调试 ELF 时按合成地址下断点（W=64 档案）：
(gdb) break *0x544300000010
# W=32 档案对应：break *0x54430010
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
| `toolchain.cli_matrix` | §5.2 工具适用面矩阵、辅助开关随主开关与孤儿拒绝、开关取值非法、编译器 CLI `--include` 数量上限 32、§5.2.1 组合矩阵（含 `w32 --gdb-elf-out` 70）的退出码断言 | `toolchain`, `fast` |
| `tc.cli` | `tc --init-project`/`--test`/`--version`/`--help`、init 的 `tests` 真实目录预检、init create/write/close 故障回滚、`--test-timeout` 取值与非法值、操作顺序与操作前 `--`、三入口契约与退出码 | `toolchain`, `fast` |
| `tc.manifest` | `tc.project` 语法、默认值、大小上限 4 MiB、include 总数上限 32、根发现（含不可读/损坏 manifest 不跳过）、非法字段与 include 顺序 | `toolchain`, `fast` |
| `tc.test_runner` | `tests/*/main.tc` 发现与 `tests` 树内 symlink、`tests` 树外/跨测试目录 2、`#lib` 根入口 2、侧车缺失/不可用/格式、trap 字符集/空白/exit 与 4 KiB 上限、过滤、header/0 tests/中止输出、timeout 报告、失败详情与捕获上限 | `toolchain`, `fast` |
| `tc.test_worker` | 每个测试 fork worker、stdin 来自 `stdin.bin`/空输入 EOF、输出捕获、超时终止与后续继续、大输入/大输出、非超时异常/协议损坏 | `toolchain`, `fast` |
| `toolchain.project_paths` | 诊断/trap/IR/trace/linemap、include 根不存在/空候选（跳过，非路径错误）、`module`/`callee_module` 的 external/`../`/`./<basename>#<n>` 回退（含空格/`]`）、根内路径 `lib.tc#1` 与回退 `./lib.tc#1` 的消歧、VM 调试 ELF `.file`/行表与 AOT 汇编字符串根相对、无绝对路径、external、路径字符（Cc/Cf/Zl/Zp）；只使用 `--emit=asm`/`--static-only`/`--gdb-elf-out`，不调用 clang/lld/gdb/readelf | `toolchain`, `fast` |
| `toolchain.project_paths_aot` | ELF/flat 源字符串与板端 trap 记录根相对、无绝对路径；AOT ELF 符号为内部 `tc_main`/`tc_function_<func_id>` | `toolchain`, `aot` |
| `toolchain.output_aliases` | 多个输出开关：字符串相同、symlink、hardlink、相对路径形式、输出与源/manifest/stdin 别名均 2；不存在目标按规范化父目录判断；写前复核与写入同 fd | `toolchain`, `fast` |
| `toolchain.project_repro` | 两个不同根目录构建 asm/ELF/flat 逐字节一致 | `toolchain`, `slow` |
| `toolchain.ir_dump` | `--dump-ir` 文本 golden（结构化 `top`/`f<id>`/`m<id>`/`struct#id`/`c<id>`/`o<id>` 段），两个工具一致；`o<id>`/`c<id>` 只在完整 dump 出现 | `toolchain`, `fast` |
| `toolchain.trace_golden` | trace 文本/JSONL golden（含失败访问只 trap 无 mem；read io.bytes 逻辑消费）（`kind`/`module`/`func`、顶层 `<top>`、v3）；同一指令 `ir`+事件/`call` 的 `enter`+`return` 多对一；重复运行字节一致；多输出共用 stderr 的时间序（§5.1） | `toolchain`, `fast` |
| `toolchain.stats` | VM 运行时统计与 AOT 静态统计字段；read io_bytes 逻辑消费口径；外部工具链失败（70）时目标派生统计已输出；内部发射失败（71）或发射 OOM（70）时只有 IR 派生字段，不输出目标派生字段 | `toolchain`, `fast` |
| `toolchain.diag_json` | 文本/JSON 同序同码同位置；trap 旧字段顺序保持、`kind`/`module` 追加，JSON 追加字段顺序断言；module 含空格/`]`/external 回退时文本标记歧义、JSON 仍可解析 | `toolchain`, `fast` |
| `toolchain.linemap` | 行表覆盖 IR 全序、`(kind,module,func,bb,insn)` 唯一、顶层 `<top>`/函数名、v3、module 的 external/`../`/回退/空格路径、trace 逐条 span 比对、空 span 视为实现错误、根相对路径与 external 标记 | `toolchain`, `fast` |
| `toolchain.debug_names` | top 单元 + `#lib public func top` + 跨模块同名函数：trace/linemap 的 `kind`/`module`/`<top>`、call `callee`/`callee_module`（含 `call_indirect` 跨模块目标；`callee_module` 与调用点 `span.file`/`module` 不同）、trap 的 `kind`/`module` 与字段顺序、`monitor funcs`/`frames`/`vreg`/`trap` 的 `module`/`func` 顺序与排除单元、模块显示路径含 `:` 的两个同名函数、`monitor frames`/`vreg`/`trap` 的 module 选择器（内部 RSP 客户端，连接/命令超时）、`monitor ir` 的 `%N`/`f<id>`/`struct#id`/label 结构操作数、VM 调试 ELF 源码符号（内部 writer/内部解析）、IR dump 结构名；新增无关函数后身份不变 | `toolchain`, `fast` |
| `toolchain.codegen_invariance` | 代码段/ABI 语义与基线一致；源路径字符串根相对；跨根产物一致 | `toolchain`, `aot` |
| `toolchain.member_identity` | 跨模块同名 `func`/`const`/`static`/`struct` 夹具：调试面只暴露函数；`monitor funcs` 不列成员/单元；`monitor objects` 不出现源码名且 object id 标记内部；IR dump 保持 `m<id>`/`struct#id`/`c<id>`/`o<id>`/`f<id>` 结构名；结构体同名字段由 owner type 区分（IR dump 结构输出）；`monitor ir` 的 `%N`/`f<id>`/`struct#id`/label 为结构操作数（内部 RSP 客户端，连接/命令超时）；完整 `--dump-ir` 的 `o<id>`/`c<id>` 仍为结构段 | `toolchain`, `fast` |
| `toolchain.debug_elf` | VM `--gdb-elf-out` 内部最小 ELF writer：§6.8 表 class/machine、e_type/节布局（spike 记录）、symtab/行表、独立解码器或 byte-exact golden（非 writer 自序列化）、确定性字节；PATH 去除 clang/lld/gdb/readelf 仍可生成；输出失败 70，`w32` 70 | `toolchain`, `fast` |
| `vm.gdb.protocol` | RSP 握手/扩展模式 `!`/PacketSize=0x4000 与 qXfer 前缀/长度边界/线程选择（`Hg0`/`Hc-1`）/断点（含同址重复、step-over、停止/终止态增删、`monitor next`/`finish` 断点交互）/单步/`\x03` 中断/寄存器读写（W=32 仍为 64 位/16 hex，`pc` 高 32 位为 0；`P pc`/`Z0` 高 32 位非 0 返回 `E01`）/内存写禁区/`RXX` 复位（旧 trace 段 abandoned、不补 footer）/running 非法帧丢弃/重同步/`D` 与 `k`/`vKill` 生命周期/`monitor`（含 vreg 类型宽度、`kind`/`module`/`<top>` 选择器、monitor funcs/frames/trap 的 module 输出路径（含空格/`]` 的固定字段顺序 golden）、module 含空格/`:`/`]`/逗号的原始剩余串解析、`monitor vreg` selector 的 external/`../`/`./<basename>#<n>` 回退、深层调用暂停/继续、`monitor next`/`finish`、trap 帧保留、未写 vreg E01、`monitor ir` 的 `0x` 十六进制地址字面量（1–16 位、大小写、无 `0X`/裸 hex/十进制/下划线、W=32 高 32 位非 0 → `E01`）与 `%N`/`f<id>`/`struct#id`/label 结构操作数；内部 RSP 客户端带连接/命令超时）/退出/重同步 | `toolchain`, `fast` |
| `vm.gdb.semantics` | 有/无调试器三种场景语义一致 | `toolchain`, `slow` |
| `vm.gdb.e2e` | 真实 gdb 端到端（§6.10，含 `Hg0`/`Hc-1`、加载 VM 调试 ELF `.symtab`/`.debug_line`、`break top`、跨模块同名函数 `break <name>` 非唯一与 `break <file>:<line>` 命中、真实 gdb restart `RXX`）；gdb 缺失时显式跳过并在 release audit 记录 | `toolchain` |
| `toolchain.debug_smoke` | qemu gdbstub 冒烟（§5.8）；gdb/qemu 缺失时显式跳过 | `toolchain` |
| `toolchain.reduce` | 最小化器收敛且指纹不变；候选同目录、临时文件清理、stdout/`--output` 结果、多模块 import 保留、候选名闭包 import 冲突（含未解析 import 名）、候选 n 从 1 递增、`--stdin`/`--output` 路径冲突、stdin 每轮从头读取、超时终止整组（含后代/管道）、候选生成 deadline、清理失败、缺 `--`/`<tool>`/`<source.tc>` 2、source 非首位 2、未知/多余参数 2、选项重复/空值 2 | `toolchain`, `slow` |
| `toolchain.presets` | CMakePresets 可配置 + `-L fast` 子集通过 | `toolchain`, `fast` |

- 标签集合固定为 `unit`、`conformance`、`toolchain`、`aot`、`slow`、`fast`；每个测试至少
  一个标签。`fast` 表示不依赖外部工具链（gdb、qemu、clang、lld）且不含 scale/longterm/
  slow 的测试集合，用于预提交；不以固定时长为契约。`fast` 与 `slow` 互斥，同一测试
  不得同时带这两个标签。
- 可选外部依赖（gdb、qemu、clang、lld）缺失时按既有策略：配置期失败或测试显式跳过，
  不得静默通过。

### 7.2 0.0.47 退出标准

- T0–T6 与工程模型 P0–P5 全部完成，G11 PASS，`docs/gates/README.md` 更新。
- 既有门禁零回归：`ctest` 全量、golden、确定性、`docs.cleanup`、发布审计。
- 新增测试项全部进入 `ctest`；`fast` 子集在 CI 上通过；不以固定时长为契约。gdb/qemu/clang/lld
  缺失导致的跳过必须在 release audit 中显式记录，不记为 PASS。
- `tc --test` worker/超时/输出：所有测试 fork；stdin 不继承终端；`--test-timeout` 到期
  失败 1 且后续继续，缺省/`0` 不限，非 ASCII 数字、溢出或缺值、单独出现/与
  `--init-project` 同现为 2；操作前 `--` 为 2；header/0 tests/中止输出、侧车缺失/不可用、
  symlink 与捕获上限按工程模型 §6.1–§6.4；fork 失败 70，worker 非超时异常/协议损坏 71；
  结束后无残留进程或临时文件。
- 三入口模型生效：`tc`/`tc-vm`/`tc-aot --version` 打印工具链 0.0.47 与语言 0.0.46；
  契约检查仍断言 51 opcode；release audit 更新为三入口。
- 可复现：两个不同根目录构建同一工程，asm/ELF/flat 逐字节一致；`tc --test` 报告稳定。
- 路径：诊断/trap/IR/trace/linemap、`module`/`callee_module`、`monitor funcs`/`frames`/`trap` 的 module、`monitor vreg` 的 module 选择器、AOT 字符串均为工程根相对，不出现绝对路径；路径组件
  为合法 UTF-8 且不含 Cc/Cf/Zl/Zp；root 源路径为 FIFO/设备 → 70；import 非普通候选
  跳过，全部无匹配报 `TC_CE_IMPORT_NOT_FOUND`；单源文件超过 4 MiB → 71；回退
  `./<basename>#<n>` 在同一编译内稳定。
- `monitor ir <addr>` 只接受 `0x` 前缀的 1–16 位十六进制；`0X`/裸 hex/十进制/下划线/溢出返回 `E01`，W=32 高 32 位必须为 0；省略参数使用当前 pc。
- RSP 寄存器恒为 64 位编码；W=32 下 `pc` 值域为 32 位合成区间，但 `g`/`p` 仍为 16 hex/寄存器，
  `P pc`/`Z0` 高 32 位非 0 或越界返回 `E01`；`m`/`M` 地址同理按 32 位有效地址零扩展。
- AOT stats：外部工具链失败 70 时目标派生字段已输出；内部发射失败 71 或发射 OOM 70 时只输出 IR 派生字段。
- 调试实体：trace/linemap v3；顶层单元 `kind=unit`、`module` 为 root 模块显示路径、`func=<top>`；
  函数 `kind=function`、`module` 为模块显示路径、`func` 为源码名；linemap 中
  `(kind,module,func,bb,insn)` 唯一，trace 以 `seq` 唯一且同一五元组可多条（多对一）；
  `callee`/`callee_module`、trap/monitor 同步；空 `module`/`func` 视为实现错误；调试 ELF 保留源码函数符号
  （VM `--gdb-elf-out` 的 `break top` 可用；跨模块同名函数用 `break <file>:<line>`；AOT ELF
  仍用内部 `tc_main`/`tc_function_<id>`），IR dump 保持结构化 `top`/`f<id>`。VM
  `--gdb-elf-out` 由内部最小 writer 生成，不调用 clang/lld/gdb/readelf；writer 与
  VM 调试 ELF 行表由 `toolchain.debug_elf` 覆盖，源码符号由 `toolchain.debug_names`
  覆盖，`.file`/行表路径由 `toolchain.project_paths` 覆盖；trace/linemap/trap/monitor 与
  IR dump 由 `toolchain.debug_names` 覆盖；AOT ELF 内部符号由 `toolchain.project_paths_aot`
  覆盖。当前调试面只暴露 `unit`/`function`；`const`/`static`/`struct`、结构体字段、局部变量
  不进入 trace/linemap/trap/monitor/调试 ELF。未来成员级输出按 `(member_kind,module,name)`，
  字段按 `(module,owner_struct,field_path)`，局部按 `(module,function,scope,name)`；
  `monitor objects` 的 object id 明确为内部标识。`toolchain.member_identity` 覆盖这些边界。
- `module`/`callee_module` 使用 §5.9 显示路径，包含 external、`../` 与 `./<basename>#<n>` 回退；
  trap 文本是 human view，路径含空格/`]`/逗号时机器解析必须用 JSON；`monitor` 输出同样是
  human view，module 路径歧义时机器消费方按已知夹具/字段顺序断言，不依赖通用空白切分；
  `monitor vreg` selector 是 `vreg` 后的原始剩余串，允许空格；`monitor ir` 的
  `%N`/`f<id>`/`struct#id`/label 是结构操作数。
- 输出别名：同一 inode 的 symlink/hardlink、输出与源/manifest/stdin 别名退出 2；不存在目标按规范化父目录+最终组件判断。
- `tc-reduce`：候选与源同 canonical 目录、临时文件清理；stdout/`--output` 结果按原字节；
  多模块 import 保留、候选名按 canonical 路径避开闭包 import（含未解析 import 名）与
  `--stdin`/`--output` 路径、n 从 1 递增；超时与候选中 70/71 按 §5.12 输出当前最佳
  （种子 70/71 或无法启动不输出；种子超时输出输入）。
- 文档同步：本文件、[TC 工程模型 0.0.47](TC工程模型-0.0.47.md)、`README.md`/`README.en.md`、
  `docs/README.md`、`docs/gates/README.md`、`AGENTS.md` 导航、`tests/README.md`、
  `src/README.md`（新增 `src/debug/`、`src/project/`、`src/base/path.*` 边界）、
  `src/CONTRACTS.md`（新内部接口、worker 结果协议与格式约定）、`CHANGELOG`。

## 8. 风险与限制

| 风险 | 处理 |
| ---- | ---- |
| gdb 版本差异（ack 模式、`vCont`、`qXfer` 分块、自定义寄存器集） | 以最小子集实现；协议套件自测；§8.1 spike 给出兼容矩阵 |
| gdb 把 `T0b` 显示为 SIGSEGV | 有意选择；§8.1 spike 验证 `?`/`c` 与退出状态；终止态仍由 `?` 报 `W<hh>`；必要时降级 `T05` + `monitor trap` |
| 路径含控制/格式字符或非 UTF-8 破坏文本或 JSON | §5.9 路径组件校验；CLI/manifest 与名称违反 2，其它发现路径违反 70 |
| `tc --test` 大输出耗尽内存或磁盘 | stdout 流式比较并限量保留；stderr 限量捕获；trap/exit 单文件 4 KiB；失败详情截断并标记；临时文件统一清理 |
| `tc --test` 侧车/`main.tc` 是 symlink/FIFO | 只读路径跟随 `tests` 树内 symlink；测试目录外 2；解析后必须是普通文件；不可用/读取失败 70 |
| `tc --test` 路径检查/打开竞态 | 项目目录视为可信；实现使用 open/fstat 或 openat/O_NOFOLLOW 等原子校验 |
| root/import 源路径是 FIFO/设备 | root 必须是普通文件，否则 70；import 非普通候选跳过，全部无匹配报 `TC_CE_IMPORT_NOT_FOUND` |
| gdb 的 `next`/`finish` 依赖架构与栈展开 | 不作为验收项；以 `monitor next`/`monitor finish` 提供等价能力 |
| VM 单步性能（每指令一次往返） | 单步仅用于定位；批量观察用 `--trace`；失控由调试器侧 `\x03` 中断停在指令边界 |
| 合成地址与语言值域混淆 | 合成区间独立且文档化；调试 ELF 与 trace 均标注为调试视图 |
| `monitor ir` 地址字面量实现各自选择 | 固定为 `0x` + 1–16 位 hex；拒绝 `0X`/裸 hex/十进制/下划线；`vm.gdb.protocol` 覆盖 |
| 发射 OOM 与 stats/退出码错配 | 三后端增加 OOM 状态；OOM→70 且只输出 IR 派生 stats；外部工具链失败 70 才输出目标派生字段；内部契约错误 71 |
| W=32 RSP 寄存器编码被误缩为 32 位 | `target.xml` 恒为 64 位；`pc` 值域收缩但编码不变；`vm.gdb.protocol` 覆盖高 32 位为零/越界 E01 |
| VM 调试 ELF 内部 writer 的 e_type/节布局与 gdb 兼容性 | class/machine 按 §6.8 表固定；§8.1 spike 确认 e_type/节布局/基址与 gdb 加载；只调整内部 writer，不改为外部 linker |
| 行表与实际执行不一致（call 原子性、无内联） | 行表以 IR 指令为单位；以 trace 记录逐条比对 span；call 映射到调用点 |
| 顶层单元与函数 `top` 同名 | 顶层单元 `<top>`/`kind=unit`，函数源码名/`kind=function`；linemap 键为 `(kind,module,func,bb,insn)`，trace 以 `seq` 唯一 |
| 调试显示名污染 DWARF 符号 | `module`/`func`/`kind`/`<top>` 仅用于 trace/linemap/trap/monitor；VM 调试 ELF 只写源码函数名；AOT ELF 符号为内部 `tc_main`/`tc_function_<id>` |
| 跨模块同名成员身份漂移 | `module` 为根相对显示路径（`tc_path_display`）；trace/linemap v3 使用 `(kind,module,func,bb,insn)`；`TcFunc.id` 不进入调试显示 |
| 结构体同名字段被误当模块成员 | 字段身份为 `(module,owner_struct,field_path)`；当前不进入调试面；IR dump 的 struct/fields 仍是结构输出 |
| object id 被当作稳定源码身份 | `monitor objects` 明确 object id 为内部标识，不承诺跨构建稳定；未来源码对象用 `module` + object path |
| 模块路径含 `:`/空格破坏 monitor 选择器 | selector 是 `vreg` 后的原始剩余串，允许空格；从右解析，函数名不含 `:`；模块路径按 §5.9 显示路径；机器接口优先 JSON/内部 API |
| monitor 文本的 module 路径含空格/`]`/逗号 | monitor 输出是 human view；机器消费方按已知夹具/字段顺序断言，不做通用空白切分；monitor 无 JSON 通道 |
| trap 文本的 `module` 路径含空格/`]` 破坏后缀解析 | trap 文本是 human view；机器解析必须用 `--diag-format=json`；测试覆盖空格/`]`/回退路径 |
| external/回退 module 身份跨编译模式变化 | `./<basename>#<n>` 按 §5.9 编译内稳定、跨模式可不同；不承诺跨模式稳定；普通根内/`../` 路径稳定 |
| `monitor ir` 的 `%N`/`f<id>`/`struct#id`/label 被当成实体身份 | `monitor ir` 是原始 IR 文本；调试身份只用 `(module,func)`/`kind`，这些都是结构操作数；`o<id>`/`c<id>` 只在完整 `--dump-ir` |
| 调试写内存破坏不变量 | 默认禁写；仅 `--gdb-allow-write` 开放，并限定区 |
| 调试器主动 kill 请求 | v1 不主动杀死进程：`k` 无应答空操作，`vKill` 运行态回 `E01`；用 `D` 分离或 Ctrl-C 结束 |
| 调试信息破坏可复现 | 工程根相对路径 + 无时间戳 + `-fdebug-compilation-dir=.`；T6 断言两次构建字节一致 |
| trace 体积过大 | `--trace-limit` 截断并标记；默认关闭 |
| `tc-reduce` 候选位置改变根/import | 候选与源同 canonical 目录、不写工程外临时目录；多模块 import 回归 |
| 工程根/嵌套工程发现歧义 | 最近 `tc.project` 生效；嵌套工程允许；文档写清 |
| AOT `.asciz` 路径字符串改变产物字节 | 0.0.47 不承诺 0.0.46 路径字节；代码/ABI 不变；跨根一致为验收 |
| gdb 不在工程根启动时找不到源 | 文档要求从根启动或设置 `directory`；T2/T6 验证 |
| `tc` 第三入口改变释放面 | 同步 release audit、AGENTS/README 与门禁；三个工具角色固定 |
| `tc --test` worker 的 fork/pipe 死锁或僵尸 | 父进程并发排空捕获或使用工程外临时文件；所有路径统一 `waitpid`、关闭 fd；超时强制终止并回收 |
| `tc --test` 超时测试不稳定 | 验收只断言 timeout 失败与后续继续，不断言精确耗时；测试使用宽松秒数 |
| `tc --test` worker 依赖 POSIX fork | 0.0.47 只在 POSIX 宿主提供 worker；其它平台另案 |
| worker 崩溃/协议损坏被误判为通过 | 结果协议校验；异常终止或协议损坏退出 71 并停止套件 |
| 外部 clang/lld 诊断含绝对路径 | 属外部工具输出，不纳入 TC 路径契约 |
| VM 调试 ELF 被外部工具链污染 | `tc-vm --gdb-elf-out` 内部 writer；PATH 去除 clang/lld/gdb 的 fast 回归；AOT ELF 仍走外部工具链 |
| macOS 缺少 gdb | 协议套件不依赖 gdb；`vm.gdb.e2e` 显式跳过并在 release audit 记录，不记为 PASS；LLDB 机会性验证 |

### 8.1 开工前 spike

T3 开始前确认以下事实，结论写入本文件对应小节，不改变里程碑边界：

1. 自定义 `target.xml`（无 `<architecture>`、4 个自定义寄存器）在目标 gdb 版本上的行为；
2. 显式指定架构时的寄存器集与断点行为是否仍可用；
3. 验证 §6.8 表的 ELF class/machine/endianness；确认 `e_type`、节区顺序、最小节集、
   合成段基址、`.symtab`/`.debug_line` 与 gdb 符号加载结果；确认 `--gdb-elf-out` 不依赖
   clang/lld/gdb/readelf，并记录最小节集与 DWARF v4。若内部 writer 不可行，先调整内部
   节集或经评审降级；降级时必须拆分 `fast` 标签；
4. `qXfer` 分块、`QStartNoAckMode`、`vCont`/`vCont?`、扩展模式 `!` 与 `RXX` 在目标 gdb
   版本上的实际交互；
5. LLDB 对同一 RSP 子集的可用性；
6. `T0b`（SIGSEGV）在目标 gdb 版本上的显示、`?`/`c` 后的退出状态，以及 `monitor next`/
   `finish` 触发 trap 时的表现；确认 T0b 只作直接停止应答、终止态由 `?` 报 `W<hh>`。

## 9. 0.0.48 移交

- RTOS 方向的三层划分（单线程平台能力、中断语义、并发与调度）与冻结约束分析移交
  0.0.48 的范围文档；本分支不预埋语言钩子。
- 本分支的 trace、行表与 VM 调试接口是 0.0.48 验证中断时间线与上下文切换的复用基础；
  0.0.48 不得为 RTOS 改动 0.0.47 已定的工具输出契约，只能增量扩展。
- 工程模型的移交：`tc.project` v1、`tc` init/test、根相对路径与 `tests/*/main.tc` 侧车
  是后续 `tc --build/--run`、workspace 与依赖管理的基础；0.0.48 的 board/device 配置
  不进入 `tc.project`。
- 移交的可扩展点：trace `kinds` 只增不减、`detail` 为开放对象、`monitor` 命令只增不减、
  GDB RSP 子集只增不减、manifest 字段按版本追加。

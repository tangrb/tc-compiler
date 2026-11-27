# TC 开发调试工具链 0.0.47

本文按三层组织：§1、§3、§5–§6 是规范契约，规定可观察行为、错误优先级与输出格式；§7 只列
验收证据；§8 保留实现约束与范围限制。契约章是唯一权威，测试与风险条目不得改写语义。

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
| **T1** 可观测 | `--dump-ir`、`--trace`（IR/call/mem/io/trap）、`--stats`、trap 详情；新增输出路径错误复用 T0 映射 | T0、P0、P2、P4a | trace 重复运行字节一致；正常/语言诊断路径的 stdout 与退出码不变；新增 golden |
| **T2** 源级映射 | 消费 P4a 共享行表，完成 AOT `--debug` CLI 集成与 DWARF v4 CU、`--dump-linemap` 与 qemu gdbstub 冒烟 | T1、P4a | 每条 IR 指令可映射到源码 span；路径根相对；同构同深度且 display 序列相同的跨根构建产物一致；真实 gdb `info line` 与断点脚本可复现 |
| **T3** VM 调试 | 复用并扩展 T1 observer、合成代码地址、GDB RSP v1（§6）、协议测试客户端 | T1、P4a | 协议套件通过；断点/单步/内存读/退出状态可断言；无观察者时零额外分配 |
| **T4** 机器接口 | `--diag-format=json`（JSONL）；诊断 schema；与 80 码表一致 | T1 | 与文本诊断同序同码；schema 稳定并有 golden |
| **T5** 测试基建 | CMakePresets、CTest 标签、`-L fast`、统一 `tests/run.py`、`tc-reduce` 最小化器 | ∥ T1 | CI 与本地同入口；标签覆盖全部用例；最小化器对种子缺陷收敛 |
| **T6** 可复现发布 | 调试产物可复现（根相对路径/时间戳）、发布审计新增工具链检查 | T2 → T3 | 连续两次构建字节一致；审计项通过 |

工程模型（`tc`、`tc.project`、测试布局、根路径）是独立轨道 P0–P5，见
[TC 工程模型 0.0.47](TC工程模型-0.0.47.md) §11；P0 先于 T1，P2（路径模型）是 T1/T2
的前置，P4a（共享行表与 AOT `.asciz`/`.file`/`.loc`/comp_dir）先于 T1/T2；P4b 的 VM
调试 ELF 与真实 gdb 源级验收在 T2/T3 之后完成，不反向阻塞 T2/T3。本节 T1–T6 只列调试/
可观测能力，不重复工程工具本身。

### 4.1 实现边界

| 里程碑 | 主要实现面 |
| ---- | ---- |
| T0 | 共享版本与私有退出码映射、CLI/驱动、发布审计 |
| T1/T2 | 驱动开关、VM observer、IR/后端统计、共享行表与 AOT DWARF |
| T3 | VM observer、GDB stub、原子 stdout 期刊与协议测试 |
| P4b | 内部调试 ELF writer、真实 gdb 源级加载/断点与证据产物 |
| T4 | 诊断 JSON 写入与 schema golden |
| T5 | CMake presets、CTest 标签、统一测试入口与 `tc-reduce` |
| T6 | AOT 路径/编译目录、确定性套件与发布审计 |

AOT `.loc`、trace `span`、VM 调试 ELF 与 `--dump-linemap` 必须调用同一行表模块；不得由
各后端自行拼接。工程工具的模块边界见[工程模型](TC工程模型-0.0.47.md) §8。

退出码只保留一个私有映射，所有入口、`run_source` 与 worker 共用；可观测输出的创建/写入
失败按 §5.1 映射为 70，不得引入本地 71 分支。具体函数边界与 `TcImplError` 细分在实现时
写入 `src/CONTRACTS.md`，测试覆盖 0/2/70/71 及环境、内存、上限错误的区分。

### 4.2 关键路径

- 关键路径：P0 → P1 → P2 → P4a → T1 → T2 → T3；P4a 同时提供 T1 trace 与 T2 AOT
  所需的共享行表，P4b 在 T3 后完成源级调试验收。
- T2 的外部工具链（clang/lld/qemu/gdb）是主要不确定源；其版本差异在 §8 的兼容性
  验证中先行确认，不改变里程碑边界。

## 5. 接口契约

### 5.1 输出通道与退出码

- `stdout` 承载：`tc-vm` 的被运行程序输出；`tc-aot` 默认 `--emit=asm` 且未给
  `--output` 时的目标汇编；`tc` 的 init/test 报告；`tc-reduce` 未给 `--output` 时的最终
  最佳源（§5.12）；以及 `--help`/`--version`。
  所有新增编译器工具输出（IR dump、trace、stats、诊断、调试服务信息）一律走 `stderr`
  或显式路径；`tc-vm` 的 stdout 字节与 `tc-aot` 的汇编字节不受这些工具输出影响。
  `tc` 的输出不属于编译器 stderr 排序流，格式见工程模型文档 §5–§6。
- `tc-vm --gdb` 的显式 `--gdb-stdout-journal=<dir>` 是程序 stdout 的调试适配器：TC 逻辑
  `write`/`writeln` 的字节与顺序不变，但宿主可见字节流定义为期刊目录中已提交记录文件的
  顺序拼接，不直接写 fd1。普通 `tc-vm` 不使用期刊；未提供期刊的 `--gdb` 因普通 stdout
  无法证明零可见失败而在首指令前退出 70。期刊契约见 §6.3。
- `tc-aot --emit=obj|elf|flat` 仍要求显式 `--output=<path>`；缺失按用法错误退出 2。
  只有 `--emit=asm` 可省略 `--output` 并写 `stdout`。
- 多路输出共用 `stderr` 时，每次运行及 RXX 开启的每个新 trace 段内按阶段时间序排列：编译期输出（诊断，其次 `--dump-ir`，再次
  `--dump-linemap`）→ 若编译成功并进入 VM，`--gdb` 会话行（见 §6.1）→ trace 记录流
  （含 footer）→ trap 报告行 → stats；显式 `--trace-out`/`--stats-out` 分流后互不交错，
  不参与排序。`tc-aot` 的 clang/lld 诊断属于外部工具输出，不参与上述 TC 阶段排序；
  它们可以在已按 §5.5 输出的目标派生 stats 之后出现，外部工具失败不撤销已输出 stats。
- 工具输出必须确定性：不含绝对路径、时钟、PID、临时路径、随机数、宿主地址；源路径一律
  工程根相对（§5.9），程序可见地址打印为区名加区内偏移。GDB 合成代码地址、调试 ELF
  地址，以及 RSP 内存映射和 `monitor regions` 返回的档案区基址属于调试协议地址，不属于
  该程序值显示规则；合成地址按 §6.2 的档案 W 调试视图呈现，档案区基址按平台档案原值
  呈现，均不是宿主地址。`--gdb` 会话行（§6.1）含系统分配的端口，以及 `tc --test`/
  `tc-reduce` 显式 timeout 是否到期，取决于运行时序，不参与逐字节比较；timeout 报告文本
  本身仍不含实际耗时、PID 或临时路径。
- JSON/JSONL 对象的键按各自 schema 示例的顺序输出；键顺序与字段顺序一样属于逐字节
  确定性契约。文本模式的行内字段解析规则见各小节。
- JSONL 只保证各自流内的记录是合法 JSON；同一 `stderr` 上的非目标输出（IR dump、
  linemap、`--gdb` 会话行、stats、文本 trap 等）仍按原格式出现。机器解析必须按 §5.1 的
  阶段顺序跳过非目标行，或用 `--trace-out`/`--stats-out`/`--dump-ir=<path>`/
  `--dump-linemap=<path>` 显式分流；`--diag-format=json` 不提供诊断专用输出路径。
- 新增开关默认关闭。0.0.47 按清洁设计**不保留 0.0.46 的绝对路径文本/产物字节**：
  程序 stdout、退出码、trap 选择与 ABI 语义不变；工具文本与产物中的源路径统一工程根
  相对（§5.9）。工程模型的默认路径行为不是开关。
- 输出身份保护中的编译器 `stdin` 指实际宿主 fd0（打开时取其 `fstat` 身份）；`tc-reduce`
  另保护 `--stdin` referent 与其私有快照。fd2 与 fd0 同一身份时同样适用输入别名保护。
- 入口在首次打开任何文件或创建任何内部 fd 前记录 fd0/fd1/fd2 的原始开闭状态；工具内部
  fd 一律从 ≥3 分配，不得复用关闭的 stdio 编号。已关闭的 fd1/fd2 不构成身份别名，但相应
  报告/结果/诊断写出失败按 70 处理；`tc-reduce` 的 stderr 是 §5.12 的 best-effort 通道，
  关闭 fd2 不把已确定的用法/主失败码改成 70。
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
  `--stats-out`、`--dump-ir`、`--dump-linemap`、`--gdb`、`--gdb-stdout-journal`、
  `--gdb-elf-out`、`--diag-format`）
  重复出现 → 2；`--include` 仍是唯一可重复的路径开关，至多 32 个，且与有效 manifest
  include 根（未写时含缺省 `src`）合计不超过 32（工程模型 §3.2）；超出 → 2。无值开关（`--stats`、`--debug`、
  `--gdb-allow-write`）重复出现按幂等处理，不视为错误。
- 工具不适用的开关必须显式拒绝（退出 2），不得静默忽略；这与"部分能力不得写成已支持"
  的既有原则一致。
- 输出路径（`--trace-out`、`--stats-out`、`--dump-ir`、`--dump-linemap`、`--gdb-elf-out`、
  `--output`）无法创建或写入失败视为环境不可用，退出 70；路径参数形式非法退出 2。
  除 `tc-aot --output`/`tc-reduce --output` 明确指向 fd1 的正式结果通道外，解析后的
  已有目标只允许普通文件；目录、FIFO、设备与 socket 均按路径/目标类型错误退出 2。
  类型预检使用 canonical 解析后的 `stat`，必要的 open 使用 `O_NONBLOCK` 并对返回 fd 做
  `fstat` 复核；不得先做阻塞 open 再判型。因此无读端的 FIFO 在首次子工具启动或 timeout
  计时前即被拒绝，不等待对端。正式 fd1 通道沿用其结果交付契约，不按文件路径类型拒绝。
- 所有显式输出的类型与身份预检必须在前端首次诊断、IR/linemap dump 或任何目标写入之前
  完成；确认别名时固定退出 2，不因源文件同时存在语言诊断而改报 1。导入闭包尚未解析时
  允许先做可证明安全的检查，其余诊断缓存到闭包/潜在 import 身份安全证明之后；无法证明
  安全时丢弃诊断，不得为输出错误提示而写回输入。预检优先级固定为：已确定的 CLI/用法
  错误 2 → 已确认的 stdio 输入别名或 VM fd1/fd2 混流 2（静默）→ 无需创建目标即可确认的
  显式输出别名/类型错误 2 → 需要打开、创建或预留才能发现的宿主 IO 失败 70 → 源语言/静态
  诊断。预留清理失败仍按本节后续规则为 70；检查期间不得写入任何观察输出或正式结果。
- 输出目标身份不等同于字符串相等。所有显式输出路径在截断或写入前完成身份检查：
  已存在目标用打开后的 `fstat` 的 `(st_dev,st_ino)` 判断（不得只对路径做 `stat`，
  `/dev/stdout` 等别名只有打开后才能与 fd1 比较）；不存在目标按“规范化父目录 + 最终组件”
  判断，并对最终 symlink 链做允许目标不存在的解析，得到预期目标路径；相对、绝对与嵌套
  悬空 symlink 都按解析后的预期目标比较。大小写不敏感或 Unicode 归一化文件系统上的
  等价名也必须在截断/写入前判定为同一身份；不得依赖 `realpath` 字符串相等，应使用
  文件系统等价 API、已存在项打开后的 inode 比较，或短暂 `O_EXCL` 探测。最终 symlink
  链成环或无法解析按路径错误退出 2；父目录/目标访问的宿主 IO 失败按 70。
  symlink/hardlink 别名、输出与输出别名均退出 2。
  编译器观察输出的显式路径（`--trace-out`、`--stats-out`、`--dump-ir`、
  `--dump-linemap`、`--gdb-elf-out` 给出 `<path>` 时）不得与 fd1、fd2 或任何其它显式输出
  目标同一身份；未给路径时写 stderr，不参与显式输出目标互异检查，但仍受下述默认 stderr
  输入别名保护。`tc-aot --output` 与
  `tc-reduce --output` 是正式结果产物，可指向 fd1，但不得指向 fd2，且正式结果 fd1 与
  fd2 同一身份（包括 `2>&1`）一律退出 2；仍不得与其它显式输出目标或源/manifest/stdin
  同一身份（`tc-reduce --output` 等于 root 源文件的显式例外
  见下条）。此处的“源”包括 root 源、整个递归导入闭包与潜在 import 的预期目标；导入闭包
  尚未完全解析时，正式结果不得先截断目标，必须延迟到身份检查完成后才打开/截断，或先写
  私有临时项并在发现任一导入冲突时退出 2 且不发布。观察输出与源/manifest/stdin 的别名
  退出 2。
- `tc-aot` 默认 `--emit=asm` 的 fd1 是正式结果，与显式 `--output` 指向 fd1 适用同一
  源/manifest/stdin 身份保护：导入闭包（解析失败时使用容错潜在 import 扫描）完成后、首次写
  汇编前，以 fd1 的 `fstat` 与 root、
  整个导入闭包、潜在 import 预期目标、manifest 和 `--stdin` 联合比较；fd1 与 fd2 同一
  身份也退出 2，避免 `2>&1` 把诊断混入汇编。别名退出 2 且不得已写入任何汇编字节。该模式
  的诊断与编译期 stderr 输出（含 `--dump-ir`/`--dump-linemap`）先缓存到导入闭包身份安全
  性证明之后；若 fd2 与 fd1 或任一输入同一身份，则退出 2
  并静默，不得向别名输入的 fd2 写诊断。因此 `tc-aot seed.tc >>seed.tc` 与
  `tc-aot seed.tc >>seed.tc 2>&1` 均在首次汇编/诊断写入前拒绝；shell 的 `>` 已在 exec
  前截断文件，不属于工具能够撤销或检测的保证。发现冲突前可先缓冲或写私有临时项，但不得
  提前污染 fd1/fd2。
- 上述 fd2 输入保护适用于 `tc-vm`/`tc-aot` 的所有模式，不限于默认汇编：首次诊断或任何
  默认 stderr 观察输出前，将 fd2 与 root、递归导入闭包/潜在 import、manifest 与 stdin
  联合比较，确认同一身份即静默退出 2；VM 在进入执行前对已解析闭包再复核一次。因此
  `tc-aot --emit=obj --output=out.o seed.tc 2>>seed.tc` 与
  `tc-vm --trace p.tc 2>>p.tc` 均不得把诊断/trace 写回源文件。无法建立安全集合时丢弃
  缓存诊断并按原用法/环境错误退出，不得退化为写 stderr。
- `tc-vm` 的 fd1 与 fd2 同一身份（含 `2>&1`）在首次诊断或执行前退出 2，避免程序 stdout
  与诊断/trace/stats 混流；该 stdio 身份检查先于 `--gdb` 能力与期刊预检，因此
  `tc-vm --gdb demo.tc 2>&1` 固定为 2 而不是缺期刊 70。需要合并观察输出时必须使用显式
  独立路径。
- `tc-reduce --output` 等于源文件时按 §5.12 显式破坏性写入；这里的“等于”按文件系统原生
  路径等价规则判断：canonical 化父目录并保留最终组件后比较，`.`/`..`、相对/绝对拼写以及
  大小写/Unicode 等价拼写归一后相同即同一路径，最终组件不跟随 symlink。不同解析路径但
  `fstat` 同 inode 的 symlink/hardlink 仍是别名并退出 2。
- 不存在目标可在身份检查期间用 `O_EXCL` 创建空预留项；预留不得截断或写入，全部目标
  完成 `fstat` 比较后才放行。若等价拼写使 `O_EXCL` 返回 `EEXIST`，必须打开已有项并按 inode
  继续比较，不能据此绕过大小写/Unicode 别名检查。放行后 writer 必须接管同一预留 fd，
  或在删除预留后使用原子 no-replace 提交，不得按裸路径重新 open 并假定身份不变。所有在
  writer 接管前终止的路径，以及所有拒绝路径，都必须按创建时身份复核并删除本次预留项，
  不留下空产物；不得删除已被替换为其它身份的文件。`tc-reduce --output` 检查时不存在的结果
  目标只允许短暂 `O_EXCL` 探测，完成身份比较后立即按身份复核删除，不保留到缩减或提交阶段，
  从而保留最终 no-replace rename 语义。预留或探测的清理失败固定为环境不可用 70，优先于
  本可返回的别名/用法错误 2；立即停止后续写入，并按创建时身份报告是否仍有残留，不得删除
  已被替换为其它身份的文件。直接写入描述符的输出必须在写前复核并写入同一 fd；使用临时
  文件 + rename 交付的 `tc-reduce --output` 按 §5.12 的身份复核与原子提交规则处理。
- 输出目录由本机开发者控制，视为可信目录；身份检查用于防止误别名与写穿，不承诺防御
  并发改名/替换。调用期间这些目录不得被其它进程改名或替换目标；实现一旦检出目标身份
  变化即退出 70，不得按旧身份继续写入。
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
| `--gdb[=<port>]` | ✓ | 拒绝（2） | VM 调试会话；stdout 必须由 `--gdb-stdout-journal` 适配，否则首指令前 70 |
| `--gdb-stdout-journal=<dir>` | ✓ | 拒绝（2） | 调试 stdout 原子期刊；必须伴随 `--gdb`（§6.3） |
| `--gdb-elf-out=<path>` | ✓ | 拒绝（2） | 可选 VM 调试 ELF（内部 writer；`w32` 70，见 §6.8） |
| `--gdb-allow-write` | ✓ | 拒绝（2） | 默认禁写 |
| `--checked` | ✓ | ✓ | 既有语义，不变 |

辅助开关随主开关的适用面：`--trace-out`/`--trace-format`/`--trace-limit` 仅在
`tc-vm` 上有效；`--stats-out` 随 `--stats` 适用于两个工具；`--gdb-allow-write` 与
`--gdb-stdout-journal` 随 `--gdb`。主开关被拒时，配套的辅助开关同样退出 2；辅助开关
未伴随对应主开关时
（如 `--stats-out` 无 `--stats`、`--trace-out` 无 `--trace`、`--gdb-allow-write` 或
`--gdb-stdout-journal` 无 `--gdb`）同样退出 2。根与源路径由 manifest/root 模块决定；输出路径由对应
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
| `--gdb` | 2 | 2（工具不适用） | 2（不执行） | 正常（仅 `tc-vm`；缺期刊时首指令前 70） |
| `--gdb-stdout-journal` | 2 | 2（工具不适用） | 2（不执行） | 正常（仅 `tc-vm`；须伴 `--gdb`） |
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
  `<n>` 只含 ASCII 十进制数字、非负、不接收符号/空格/小数，溢出 → 2。首次截断时，文本模式输出一次不带计数的
  `# tc-trace truncated`，JSONL 输出一次 `{"type":"truncated"}`，随后继续执行，不改变程序
  行为；同一 trace 段不重复该标记。最终丢弃数在流的末尾给出（见本节末）。

记录字段（文本与 JSONL 同名）：

| 字段 | 类型 | 说明 |
| ---- | ---- | ---- |
| `seq` | 整数 | 每个 trace 段内从 1 递增，只统计该段实际输出的数据记录；header/truncated/footer 不占 `seq`；RXX 开启新段时重新从 1 开始 |
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
| `io` | `bytes`（成功提交的逻辑字节数；`writeln` 含末尾换行；`eof`/`error` 为 0）、`result`（`ok`/`eof`/`error`） |
| `trap` | `code`（`TC_RE_*`） |

- 非 `ir` 记录同样携带触发指令的 `op`、`bb`、`insn`、`span`：`call` 事件的 `op` 为
  实际触发指令 `call` 或 `call_indirect`（含打印后缀），`mem` 为对应访问 op（含 `.volatile`
  形态），`io` 为 `read`/`write`/`writeln`，`trap` 为触发 trap 的指令 op。记录类别由
  `detail` 键集区分；`ir` 记录不带 `detail`。
- `io` 事件在每次实际进入 `read`/`write`/`writeln` 的宿主 I/O 尝试后输出一条；成功为
  `ok`，输入在首个非空白字节前耗尽为 `eof`，宿主读写失败、Token 中途 EOF、非法 UTF-8/
  词法或数值转换/范围失败为 `error`；后两类不产生逻辑消费字节。`read` 的 `bytes`
  与 §5.5 的 `io_bytes` 增量相同；`write`/`writeln` 仅在整条记录成功提交时记请求字节数，
  失败不记入 `io_bytes`。即使结果为 `eof`/`error`，仍计入 `--trace-limit` 与 `seq`。
- `detail` 的数值编码：文本模式 `offset`/`dst_offset`/`src_offset` 用 `0x` 十六进制，
  `size`/`bytes` 用十进制；JSONL 中这些键均为十进制 JSON 整数，不写成字符串。
- `region`/`dst_region`/`src_region` 取值与 `--dump-ir`/`monitor regions` 的五区名一致
  （`frame`/`static`/`device`/`heap`/`code`）；`memmove` 的 `size` 是 `n × sizeof T`
  的总字节数。
- `mem` 记录只在地址成功解析到有效区/对象之后输出；空指针、跨区、悬垂等在解析出
  `region` 前就 trap 的访问只输出 `trap` 记录，不输出 `mem`，也不伪造 `region`/`offset`/
  `size`。若地址、对象与访问大小已成功解析，随后才因写保护、checked 未初始化读取等
  权限或状态检查失败，则按同指令事件顺序先输出 `mem`、再输出 `trap`。`memmove` 的源或
  目标解析失败同样只输出 `trap`；两者均解析成功后才发生的后续检查失败先 `mem` 后 `trap`。

`call` 的 `enter` 与 `return` 记录都以调用点指令为位置锚点：`module`/`func`/`bb`/`insn`/`span`
指向调用者的 `call` 或 `call_indirect` 指令，`depth` 取该调用点处的调用者深度（不是被调函数的深度）；
`return` 记录另带 `direction:"return"`、`callee` 与 `callee_module`。被调函数的深度由随后的
`ir` 记录体现。
同一调用点上的记录顺序固定：当 `ir` 与 `call` 同时启用时，先输出该调用指令的 `ir`
记录，再输出 `enter`；只启用 `call` 时直接输出 `enter`。被调函数最后一条 `ir` 记录
（若启用了 `ir`）之后输出 `return`。正常返回时 `enter` 与 `return` 成对；被调函数发生
trap、程序终止或 `--trace-limit` 截断时不补发缺失的 `return`。
同一条指令产生多个启用的记录时，事件记录统一排在其 `ir` 记录之后，顺序为
`mem` → `io` → `call` → `trap`；未启用 `ir` 时事件记录直接输出。调用目标/深度预检失败、
空 `call_indirect` 或尚未创建被调帧时只输出 `trap`，不输出 `enter`；只有被调帧成功创建后
才输出 `enter`，因此 `call`→`trap` 顺序不要求每次失败调用都产生不成对的 `enter`。

调试显示命名约定（trace、行表、trap 报告、`monitor funcs`/`frames`/`vreg`/`trap` 共用）：
- `kind=unit`，`module=<root 模块显示路径>`，`func=<top>`：顶层单元。`<top>` 是字面 token，不是 TC 标识符；不改变标准 §2.3，也不禁止用户声明 `func top`。
- `kind=function`，`module=<模块显示路径>`，`func=<源码函数名>`：普通函数；不同模块同名由 `module` 区分，函数 `top` 仍显示为 `top`，没有额外名字特判。
- `module`/`callee_module`/`span.file` 使用同一显示路径**格式**；回退名统一以 `./` 保留前缀开头（见 §5.9）；`module` 是当前实体模块，`callee_module` 是被调函数模块，`span.file` 是当前记录的源码位置。跨模块调用时 `callee_module` 不必等于 `span.file`。外部 `./<basename>#<n>` 回退编号按 §5.9 在编译内稳定，不同编译模式可不同，不承诺跨模式稳定。
- `(kind,module,func,bb,insn)` 是 IR 指令/linemap 键；`kind` 区分单元/函数，`module` 区分跨模块同名成员。trace 段内记录以 `seq` 唯一，同一五元组可有多条记录（`ir`+事件、`call` 的 `enter`/`return`），trace→linemap 为多对一。
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
- 只有本次运行已写出 trace header 且 sink 仍可写时，`error` 才写出 footer/end 行，之后按
  §5.1 的退出码结束；尚未进入 VM 的源读取/编译/诊断失败不输出 header 或 footer。若 sink
  本身写失败，不输出 footer/end 行，直接按 §5.1 退出 70；写入失败只记录待处理工具错误，
  不得跳过同一指令已到期的 `trap` 记录或统计更新，当前指令的 TC 提交/trap 后果完成后才以
  70 结束。RXX 终结的旧段按 §6.7 不补 footer。
- trace 的 `seq` 与 `dropped`、stats 的运行时计数均使用 64 位无符号整数；递增溢出为
  `tc_error_internal`（71），不得回绕。`executed` 在指令分派入口计数，`dropped` 在判定某条
  数据记录因 limit 未输出时计数；检测到溢出只登记待处理工具错误，当前指令已开始的 TC
  执行、I/O 提交与 trap 后果仍完成后才结束。已有输出保留，不再承诺 footer/end 或 stats
  完整。同一路径的多重工具错误优先序为：结果/正式输出交付失败 70 > trace/stats sink 的
  宿主 IO 失败 70 > 计数溢出 71；没有工具错误时才保留程序退出码或 TC trap 的 1。
- `source` 与 §5.10 一致，为 root 模块的工程根相对路径。
- 确定性：每段内 `seq` 递增；`span` 使用工程根相对路径；相同初始进程与宿主输入状态下
  重复执行产生相同字节。RXX 显式继承前段状态，其后续 trace 不参与新进程重复执行的字节
  一致断言。

### 5.5 `--stats`

- `tc-vm --stats`：运行时统计——执行指令总数、按 op 计数、最大抽象深度、五区使用峰值、
  分配次数、I/O 字节数；程序终止后输出。`executed` 是已开始执行（进入该 IR 指令分派）的
  指令数，trap 触发指令本身计入、其后的指令不计入；按 op 计数采用同一口径。`allocations`
  是成功堆分配次数，`io_bytes` 是成功提交的
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
  其后每行 `key=value`，字段顺序固定、只增不改义，供脚本解析。写出/刷新/关闭失败为 70，
  不得改报程序正常结束或 TC trap；与 trace 共用同一 sink 时只报告一次工具 I/O 错误。
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

- 前缀（`file:line:col`、`error[RT]`、码、打印名）与 0.0.46 一致；后缀字段顺序固定、
  只增不改义。文本行不承诺可按“首个 `TC_RE_*`”机器解析：`file` 与 `module` 显示路径
  可以包含 `TC_RE_*` 或 `error[RT]:` 文本，先出现的匹配可能来自路径而不是码字段。
  机器解析必须使用 `--diag-format=json`；文本模式只作 human view。
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
  `<tool>: TC_ERR_OUT_OF_MEMORY: OutOfMemory: memory allocation failed`，固定打印名与消息不得改写；
  其它实现层错误使用 `tool: <impl-error>: message[: detail]`。`detail`
  存在时只含工程根相对源路径（§5.9），不得含绝对
  路径；退出码按 §5.1。`tc-reduce` 在选项解析、根发现或闭包构造期间尚未证明 fd2 安全时，
  本节固定文本义务让位于 §5.12 的“丢弃早期诊断”规则：保持原退出码但静默，不得为满足固定
  文本向可能别名 source/import/manifest/stdin 的 fd2 写入；其它入口仍须在安全写入前提下
  使用固定文本。
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
- 仅有 `.file`/`.loc` 不足以让真实 gdb 建立源级编译单元；`tc-aot --debug` 还必须生成
  最小 DWARF v4 CU（`.debug_info`、`.debug_abbrev`、`.debug_str` 与 `.debug_line`），CU 的
  `DW_AT_stmt_list` 指向共享行表，`DW_AT_comp_dir` 为 `.`，`DW_AT_name` 使用 root 显示路径，
  并给出代码地址范围。`.debug_line` 继续由 `.file`/`.loc` 经汇编器生成，内部 writer 只追加
  CU 所需节与 `DW_AT_stmt_list` 重定位，不得再发射第二个 `.debug_line`。汇编路径由内部
  writer 或等价的确定性汇编段生成，不调用额外编译器。
  验收必须断言 `.debug_info` 非空、CU 与 `.debug_line` 的版本均为 4，且真实 gdb 的
  `info line` 能解析根相对源文件与地址。
- 一条 IR 指令 lowering 成多条目标指令时，这些目标指令共享同一个 `.loc`；
  映射粒度是 IR 指令，不是目标指令。
- 汇编仍经 clang 集成汇编；`--emit=obj|elf` 时保留行表；`--emit=asm` 时 `.loc` 可见。
- `--emit=flat` 不承载调试段：`--debug` 仍生成带 `.loc` 的中间汇编与行表，最终 flat
  不含调试信息，此为文档化行为而非错误。
- 覆盖要求：行表产物（linemap）覆盖 IR 全序的每条指令；同一函数内 `.loc` 地址严格递增。
  行表是静态集合，`--trace=ir` 是动态执行序列（循环会重复、死代码不出现），两者不比较
  条目数；验收方式是对 trace 的每条记录按 `(kind,module,func,bb,insn)` 查行表（多对一），
  断言其 `span` 与记录 `span` 相同。`kind`/`module`/`func` 使用 §5.4 的单元/函数显示命名；
  trace 段内记录本身由 `seq` 唯一。
- 汇编器可能把相邻且 `(file,line,column)` 相同的 `.loc` 合并为一条 DWARF 行表记录，因此
  `.debug_line` 的记录数可以少于 IR 指令数；覆盖验收以 linemap 为准，不按
  `.debug_line` 行数计数。
- 调用 clang 汇编/链接时固定 `-gdwarf-4 -fdebug-compilation-dir=.`，避免汇编器默认生成
  DWARF v5 行表及 `DW_AT_comp_dir` 泄漏绝对构建目录；gdb 从工程根启动，按根相对路径找
  源码。裸 `--emit=asm` 的下游使用者也必须以 `-gdwarf-4` 汇编，才能保持本节固定版本。
- 行表与 trace 要求每条指令的 `span.file` 非空且已根相对化；驱动层的 `<source>` 只是
  诊断打印的防御回退，行表生成遇到空 `file` 视为实现错误（退出 71）。
- AOT 产物内的源文件路径字符串（`.asciz`）使用 §5.9 显示路径并按汇编字符串规则转义；
  代码段、数据语义与 ABI 不变；两个同构同深度且源文件 display 路径序列完全相同的根目录
  构建同一工程时，asm/ELF/flat 必须逐字节一致；display 序列不同时不作该断言。
  0.0.47 不承诺与 0.0.46 的绝对路径产物字节一致。
- 验证工具：`llvm-dwarfdump --debug-line` 或 `readelf --debug-dump=decodedline`；
  AOT 与 VM 调试信息均固定 DWARF v4，版本号记录在测试说明中。
- qemu gdbstub 冒烟：以 `qemu-system-* -s -S` 加载 AOT ELF，gdb 连接后按源码行下断点
  并单步，确认行表被真实调试器消费。

### 5.9 工程根与路径

- 根、manifest、布局、`tc` 工具与测试约定见 [TC 工程模型 0.0.47](TC工程模型-0.0.47.md)。
- 工程根发现、manifest、模块身份、源文件/include 根类型与读取上限，以及显示路径的回退
  规则，以[TC 工程模型 0.0.47](TC工程模型-0.0.47.md) §3–§4 为唯一权威；本节只规定工具链
  对路径的消费方式。
- 文本诊断、诊断 JSON、实现/IO 错误源路径、trap 文本/JSON、`--dump-ir` 的 `modules:`、
  trace 的 span/header/module/callee_module、linemap、调试 ELF、AOT 的
  `.file`/`.loc`/`# source:`/`.asciz`、板端 trap，以及 `monitor funcs`/`frames`/`trap`/
  `vreg` 的 module，统一使用显示路径。同一编译内（含部分检查）同一文件只使用一个显示路径；
  不同编译模式的回退编号可以不同。`tc --test` 报告中的 `<test>` 是逻辑 `tests/<目录名>`，
  不是源文件显示路径；worker 的临时路径、PID 与实际耗时不进入报告。
- 根模块首个有效模式为 `#lib` 时各入口的统一退出码与优先级，以[TC 工程模型 0.0.47](TC工程模型-0.0.47.md)
  §1 为唯一权威。
- 0.0.47 不提供绝对路径/前缀映射开关；clang/lld 自身诊断中的绝对临时路径属于外部
  工具输出，不纳入 TC 路径契约。调试 ELF/AOT 的路径、编译目录与 gdb 启动规则以
  §5.8/§6.8 为准。
- 跨根 asm/ELF/flat 逐字节一致的条件以 §5.8 为唯一权威。`./<basename>#<n>` 是显示身份
  而非真实文件名，只保证行表、trace 与地址断点，不承诺 gdb 按该名字找到源码文件。

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

- 调用：`tc-reduce <source.tc> [--timeout=<seconds>] [--stdin=<path>] [--output=<path>] --
  <tool> <args...>`。工具参数中的每个 `{}` 替换为同一候选路径，不含 `{}` 时追加到末尾。
  首次运行记录首个 `TC_CE_*`/`TC_RE_*`、退出状态与终止方式，缩减过程必须保持指纹不变。
- code 只来自结构化诊断：已认证的内置 `tc-vm`/`tc-aot` 通过私有 fd 接收 JSONL，普通
  stderr 不参与指纹。完整行缓冲只接受唯一 schema 有效的 `severity=error`；note 跳过，
  非空流缺失/重复 error 或 JSON/schema 损坏为 71，不回退文本扫描。干净空流不建立指纹：
  子工具 70/71 原样传播，其它退出状态在种子阶段为 2、候选阶段不可接受。用户已含 JSON
  开关时不重复添加，含文本开关为 2。
- v1 只内置 `tc-vm`/`tc-aot`，按解析后可执行文件的 `(st_dev,st_ino)` 绑定本安装版本，
  PATH 同名包装器不算内置；认证后以同一 fd `fexecve`，不按路径重新 exec。自有参数须在
  用户首个 `--` 之前，`{}` 只在该 `--` 之后为 2；其它工具为 2，不提供插件发现。
- `fork` 后父进程关闭诊断写端、子进程关闭读端；只有内置工具子进程在 exec 前保留写端并
  清除 CLOEXEC，父进程排空到真实 EOF。驱动外部工具链前关闭诊断 fd，外部后代不得继承
  任何协议 fd。
- 选项解析、根发现与导入闭包构造期间的诊断必须先缓存，不得直接写 stderr。只有在完成
  fd2 与可确定输入身份的安全检查后才允许首次 stderr 写入；若无法建立该安全集合（包括
  缺少 source 或 source 不可读），则丢弃缓存诊断并按原用法/环境错误退出，绝不为了打印
  错误而向可能别名 source/import/manifest/stdin 的 fd2 写入。
- 首次运行前构造“递归导入闭包 + 种子容错 import 扫描”身份集合：已解析文件用
  `(st_dev,st_ino)`，潜在 import 按所有可能导入者目录/include 根保守解析并加入集合；
  SYN 失败不能漏掉其后的 import。选项/根/manifest 诊断先缓存，只有 fd2 与可确定输入
  身份完成安全检查后才首次写 stderr；无法证明安全时丢弃诊断，按原错误退出。
- 内置适配器解析显式输出（trace/stats/dump IR/dump linemap/gdb ELF/AOT output）。首次
  运行前按 §5.1 联合检查子工具输出、源、闭包/潜在 import、manifest、stdin、result、
  reducer/子工具 fd1/fd2 与其它输出；任一别名为 2 且不启动。未指定 `--output` 时 fd1 作为
  默认结果，另与源、闭包/潜在 import、manifest、stdin 做 inode 检查。每个候选 `n` 代入
  `{}` 后重检；仅候选 basename 导致的输出/候选/result 冲突递增 `n`，静态输入/result 或与 n
  无关的冲突仍为 2。`--output` 等于 root 源的例外不扩展到 import、manifest、stdin、fd2
  或子工具输出。子工具输出不得解析到其 fd1/fd2，并加入候选 `n` 跳过集合。
- reducer fd1/fd2 在首次运行前分别与源、闭包/潜在 import、manifest、stdin 检查；显式
  `--output` 时 fd2 还与 result 比较，`--output=/dev/stdout` 不构成 fd1 自比较。fd1/fd2
  同一身份（含 `2>&1`）为 2。shell `>` 的启动前截断不可撤销，但 reducer 不得继续写该身份。
- reducer 全生命周期忽略/捕获 `SIGPIPE`；stderr 失败 best-effort，不绕过清理或改变主失败
  码。子工具 fd1/fd2 接私有捕获，父进程并发排空；stdout 丢弃，stderr 仅供内置适配器结构化
  读取。内部 fd 默认 CLOEXEC，exec 前只保留 stdio、执行所需 fd 与明确交给内置工具的诊断
  fd。可执行 fd 在 `fexecve` 前清 CLOEXEC，编号经 `TC_REDUCE_EXEC_FD` 传递；工具启动外部
  后代前关闭该 fd 并移除变量。子工具 exec 前恢复默认 SIGPIPE。捕获失败为 70，逃逸后代
  不阻塞排空结束。
- `--timeout`、`--stdin`、`--output` 各至多一次；`--stdin`/`--output` 只接受
  `--opt=<path>` 形式，`--timeout` 只接受 `--timeout=<seconds>` 形式；空值或重复 → 2。
- `<source.tc>` 必须是第一个参数；缺少 `<source.tc>`、`--` 或 `<tool>`，或 `--` 前出现
  未知/重复选项或多余位置参数 → 2；`--` 后的参数全部作为 `<tool>` 及其参数，不再解析
  `tc-reduce` 选项。
- 种子使用原始 source；候选写入其 canonical 目录下 `tc_reduce_<n>.tc`，`n` 从 1 递增，
  以 `O_EXCL` 创建并避开闭包/容错扫描中的 import 名（含未解析名）、已有条目、stdin、result
  与子工具输出。basename 比较按文件系统原生目录项等价规则；不存在路径按 §5.9 的最近存在
  祖先规则 canonical 化。同目录保持根发现、manifest include 与相对 import 不变，不写工程外
  临时目录，也不修改原文件（显式 `--output` 等于源文件除外）。
- 种子侧额外做容错 import 扫描：按 LF/CRLF/CR 收集逻辑行中的 `import <ident>`，不要求整文件
  通过 SYN；非法 UTF-8/Token 只跳过该错误并从同步边界继续，无法确定的名保守保留。候选
  运行前重检闭包，basename 去 `.tc` 后不得等于自身 import 名，候选也不得与自身/闭包解析为
  同一 inode（含 symlink/hardlink）；冲突时复核创建身份、删除候选并递增 `n`。
- 创建时记录候选 `(st_dev,st_ino)`，每轮结束及退出前复核；被其它身份替换时不得 unlink，
  记清理失败。创建/写入失败为 70；预检后 `EEXIST` 复核后递增 `n`。创建后保证至少 `0600`，
  `fchmod` 失败按创建失败清理并退出 70。候选在每轮结束及退出时删除，其路径不进入指纹、
  结果或诊断。
- 默认把当前最佳源文本按原字节写 stdout，进度/诊断只写 stderr。stdout `EPIPE` 为 70，
  不以信号 141 终止。`--output=<path>` 完整解析最终 symlink 链：解析为 fd1 时直接写已打开
  fd1，不 rename；其它目标只允许普通文件或不存在的普通文件路径，FIFO/设备/socket/目录/
  解析到 fd2 均为 2 且不阻塞。
- 普通文件在 referent 所在目录以保留前缀 `.tc-reduce-result-<n>.tmp`、`O_EXCL|O_NOFOLLOW`
  创建临时文件并 rename 原子写，不替换 symlink 本身；`n` 从 1 递增并跳过已有条目及与源、
  stdin、manifest、result 冲突的解析路径，记录创建身份。提交前复核目标身份；检查时不存在的
  目标只短暂 `O_EXCL` 探测后按身份删除，提交时使用原子 no-replace rename，被抢占为 70 并
  保留对方；已有目标允许按复核身份替换。临时文件写/close/rename 失败均复核身份后清理并
  退出 70。指定 `--output` 后不再写默认 stdout。
- 不隐式就地覆盖 source；`--output` 等于 source 按路径名同一性允许显式破坏性写入，symlink/
  hardlink 别名仍为 2。默认 fd1 与输入身份的联合检查、fd1/fd2 同一身份检查按本节前述规则。
- 缩减动作：删除整条语句/声明/未使用 `import`、删除空行/纯注释逻辑行、简化字面量、
  删除不可达函数；切点必须落在逻辑行边界，不得拆分有效 UTF-8 序列或 CRLF；LF、CRLF
  与 CR 都视为行尾。每步之后重新解析。语法坏种子允许继续缩减到仍触发同一 LEX/SYN 诊断
  的候选，不要求候选整文件通过 SYN；任何候选都必须保持首次失败指纹不变。
- 被测程序的 `stdin` 由 `--stdin=<path>` 提供；该路径解析后必须是普通文件（FIFO/设备等
  → 70）。首次运行前将其完整内容复制为工程外私有只读临时文件，记录原身份/长度；种子与
  每个候选运行前从该快照路径独立 `open` 得到新的 open file description，再定位到偏移 0，
  不得通过 `dup` 共享上一轮或逃逸后代持有的文件偏移。原文件后续增长、缩短或改写不改变
  本轮输入。快照失败退出
  70；缺省为空输入（每次立即 EOF）；trap 复现同样适用。
- 工程内运行时继承 `tc.project` 的根/profile/include；manifest 读取沿用工程模型 §3.1
  （含 4 MiB 大小上限）；诊断路径按 §5.9 根相对；指纹仍按结构化接口取得的首个
  code/exit/终止方式，不按路径。
- 保证：当前最佳结果初始为输入；每轮成功缩减严格减少行数或字节数；候选按固定动作列表
  顺序、源位置升序与各简化器内部的固定取值顺序枚举，同一轮采用第一个通过指纹检查的候选，
  不依赖目录枚举、进程调度或随机数。若不存在可减少的候选，则以当前结果结束（可能等于输入），
  退出 0。不设置 `--timeout` 或设为 `0` 且被测工具确定性时，同输入必须逐字节同输出；
  只有设置正数 deadline 时，到期与否才取决于运行时序（§5.1），缩减结果不参与逐字节确定性门禁。
- 正常收敛且清理成功为 0；种子无诊断/trap 指纹为 2。`--timeout=<seconds>` 约束整个缩减
  过程，从首次调用工具开始，覆盖种子、候选生成与重跑；只接受非负 ASCII 十进制，溢出为 2，
  缺省/0 不限。deadline 与 `waitpid`/状态管道同时就绪时先回收并检查终止状态；只有尚未观察
  到终止且由 reducer 先发清理令牌才判 timeout。已先观察到的工具 70/71 不改写为 timeout。
  deadline 在决策结束时停止，交付与清理不计时，stdout 背压不构成超时。
- 每次运行由组哨兵持有独立进程组/会话，工具是哨兵子进程。哨兵报告 pid/状态并在清理令牌
  前保持存活，使工具退出后组号仍被占用；reducer 只向仍持有会话的哨兵发令牌，由哨兵向
  自身组发送 `SIGKILL`，随后由 reducer 回收哨兵，不得按工具 pid 或旧组号自行 `killpg`。哨兵死亡/身份不符时
  固定 70，只终止仍以 pid/启动时间或 pidfd 验证的直接工具子进程，不等待非直接后代，不
  建立新指纹；组不存在（`ESRCH`）不是清理失败。未确认组已建立时只回收已产生哨兵。不承诺
  终止 `setsid` 逃逸后代，捕获只排空当前可读字节，不等待其 EOF。
- 工具 70/71 原样传播；exec/fork/候选/stdin 失败为 70；除 reducer 主动 timeout 外，工具
  被信号终止为 71 且不建立指纹。timeout 交付当前最佳并退 1；种子 timeout 也交付输入退 1。
  已有指纹时，70/71、timeout 与清理前失败均交付当前最佳；种子尚无指纹的 70/71/信号/无法
  启动不输出结果。
- 失败/交付优先级：结果写 stdout/`--output` 失败始终为 70，否则保留主失败码。决策结束后
  先回收 worker、删除候选并清理临时文件，再交付最佳；清理失败不改变已有主失败码，但把
  原本成功升级为 70。除种子尚无指纹的 70/71/信号/无法启动及无指纹用法错误 2 外均交付最佳。
  已建立会话的正常退出、信号与 timeout 都经仍存活的哨兵清理同组非逃逸成员；逃逸后代和
  终止后新入组进程不在回收范围。
- `tc-reduce --timeout` 只约束缩减过程，不是 `tc --test --test-timeout` 的别名；两个
  开关互不适用，用于另一工具时按 §5.1 退出 2。
- 非目标：不做语义等价化、不改 include 根、不合成新程序。

## 6. VM 调试器（GDB RSP）

### 6.1 会话模型

- 开关：`--gdb[=<port>]`，缺省端口 0（由系统分配）。端口只接受 `--gdb=<port>` 形式，
  `<port>` 只含 ASCII 十进制数字、取值 0–65535；空值、符号/空格、越界或溢出 → 2。
  只绑定 `127.0.0.1`，不提供远程监听；端口不是独立位置参数。
- stub 在首指令前等待连接；未连接时程序不推进。等待连接、停止或重连期间宿主 Ctrl-C
  都终止整个 stub，不设调试器空闲超时。`--gdb-stdout-journal` 必须在首指令前完成目录绑定、
  独占锁与提交探针；缺期刊或预检失败均为 70，不直接使用普通 stdout。
- 会话信息固定为运行时首行 `tc-vm: gdbstub on 127.0.0.1:<port>`，写到 stderr：位于编译期
  诊断/dump 之后、trace 之前；编译失败不打印。它不属于 trace/诊断/统计流，动态端口也不
  参与确定性逐字节比较；与 JSONL/JSON 同用时解析器须跳过该行或用显式 sink 分流。
- 端口占用/绑定失败为 70；参数非法或用于 `tc-aot` 为 2。RSP 经 TCP，不占用程序 stdin；
  逻辑 stdout 按 §6.3 写入期刊，字节与顺序不变。monitor 与调试 ELF 路径按 §5.9 根相对。
- 非终止态断连或连续三次校验和失败按 running EOF/半关的可恢复中断处理：完成当前指令
  边界后关闭连接并回到监听；输出提交按 §6.3 延迟到整条结束，部分 Token 保留 pc 与缓冲。
  running EOF/半关等同一次 `\x03`，停止应答尽力写出后不再读该连接。stub 忽略 `SIGPIPE`
  或使用 `MSG_NOSIGNAL`，`EPIPE` 按断连处理且不改变退出状态；已提交的普通 `D` 或终止态
  `vKill` 不因应答 `EPIPE` 回滚。
- 终止态断连按会话模式处理：普通 `target remote` 在终止后保持连接并回答终止态报文，
  `D` 回 `OK` 后按退出状态退出，客户端断连也退出；扩展会话终止后断开则回监听等待 `RXX`。
  重连从普通 ack、非扩展模式开始，须重新发送 `!`；暂停/终止状态、断点与已提交 I/O 保留。
  扩展会话持续监听，直到显式 `k`/`vKill`、下一次终止态普通会话结束或宿主 Ctrl-C。
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
- 自然终止哨兵 `TC_DEBUG_CODE_BASE + 4*N` 不属于该区间。W=32 在 `N` 达到地址空间上界时，
  哨兵按 64 位 RSP 字段编码为 `0x00000001_00000000`；这是 §6.4 唯一的哨兵例外，
  不是程序可见地址或有效指令地址，不得用于断点、跳转或内存访问。
- 该地址区间不属于任何平台档案的五个区，不进入 `funcaddr`/`bitcast` 结果，不参与程序行为；
  它是当前档案 W 的调试视图，不是目标地址：W=64 使用 64 位区间，W=32 使用 32 位区间，
  区间必须落在对应地址空间内。W=32 的程序可见地址仍按 32 位呈现。
- `--gdb` 会话启动与 `--gdb-elf-out` 生成时校验合成区间与当前档案区表不重叠；重叠则
  实现错误退出 71，不得退化为歧义地址。
- `m` 对合成区间返回全零；断点由 stub 处理（`Z0`），不写内存。

### 6.3 报文子集（v1）

| 类别 | 报文 | 行为 |
| ---- | ---- | ---- |
| 握手 | `qSupported`、`QStartNoAckMode`、`!` | 报告 `PacketSize=4000;vContSupported+;QStartNoAckMode+;qXfer:features:read+;qXfer:memory-map:read+`（`4000` 为十六进制 0x4000=16384 个 data 字符）；支持免应答模式与 `Z0` 软断点；`!` 启用扩展模式并回 `OK`，`RXX` 仅在扩展模式可用；v1 扩展模式只提供 restart，不支持 extended-remote 的 `D` 分离 |
| 停止原因 | `?` | `T05thread:1;`（断点/单步/暂停），`T02thread:1;`（中断）；终止态（自然终止或 trap 终止）一律 `W<hh>`，trap 详情经 `monitor trap` 读取 |
| 中断 | `\x03`（带外字节，不是 `$` 包） | 运行中收到时完成当前指令并停在下一条边界，报告 `T02thread:1;`；当前指令阻塞于宿主输入时按可恢复输入规则挂起；阻塞于宿主输出提交时延迟到该记录提交结束后再停，不提供中途挂起；未运行时忽略 |
| 寄存器 | `g`、`p<n>`、`P` | `g` 按 regnum 顺序返回档案对应的完整核心寄存器集与 4 个 TC 寄存器；未建模核心寄存器返回 RSP unavailable 编码，`p<n>` 使用 §6.4 的编号与位宽。`P` 默认返回 `E01`，`--gdb-allow-write` 下仅允许在非终止态写架构 `pc`，且必须落在合成代码区间的指令边界，否则 `E01`；`G` 返回 `E01` |
| 内存 | `m`、`M`、`X` | 读五区与合成代码区；写默认 `E01`，`--gdb-allow-write` 下仅限帧/静态/堆区；跨区边界或区外的 `M` 返回 `E01`；`X` 不支持，返回 `E01` |
| 执行 | `s`、`S`、`c`、`C`、`vCont;s`、`vCont;c`、`vCont?` | 单步、继续；单步粒度为一条 IR 指令；`S`/`C` 的信号参数忽略；执行命中 TC trap 的停止应答为 `T0bthread:1;`；`vCont?` 返回 `vCont;s;c` |
| 断点 | `Z0`、`z0` | 指令边界断点，最多 64 个唯一地址；`kind` 接受 4，ARM M-profile 与 RISC-V32 另接受 GDB 使用的 Thumb/压缩指令 `kind=2`，二者都只记录 4 字节 IR 边界、不写代码内存；同址重复 `Z0` 幂等（回 `OK`，不占新槽），`z0` 对未设置地址回 `OK`；超出或非法地址返回 `E01`；`Z1`–`Z4` 返回空 |
| 线程 | `qC`、`qfThreadInfo`、`qsThreadInfo`、`Hc`/`Hg`、`qAttached` | 单一合成线程 `1`；`qC` 返回 `QC1`；`qfThreadInfo` 首次返回 `m1`、再次返回 `l`；`qsThreadInfo` 返回 `l`；`Hg0`（any thread）、`Hc-1`（all threads）与 `Hc1`/`Hg1` 都等价于合成线程 1，返回 `OK`；其他 `Hc`/`Hg` 返回 `E01`；`qAttached` 返回 `0`（程序由 stub 创建）；`vCont;s`/`vCont;c` 接受可选 `:1` 后缀，其他线程号返回 `E01` |
| 特性 | `qXfer:features:read`、`qXfer:memory-map:read` | §6.4 的 profile 对应 `target.xml`（完整核心寄存器集 + 4 个 TC 寄存器）与五区内存映射；`offset`/`length` 为十六进制；支持分块读取 `m`/`l` 语义。`PacketSize` 是 stub 接受的入站 data 字符上限；TC 对 qXfer 应答也采用同一上限。`m`/`l` 前缀各计 1 字符，有效数据 ≤ `PacketSize-1`；仍有后续数据时必须用 `m`，只有确认已无剩余数据时才用 `l`（offset 已在 annex 末尾时可返回空 `l`） |
| 监视 | `qRcmd` | §6.6 的 `monitor` 命令；应答为 `O` 分块 + `OK`/`E01`，不以 `W` 应答；每个输出包 data 不超过 `PacketSize`，`O` 前缀计 1 字符，十六进制内容按偶数边界分块且最多 16382 字符，最终 `OK`/`E01` 单独发送 |
| 生命周期 | `D`、`RXX`、`k`、`vKill` | `D` 是显式分离，仅普通 `target remote` 会话支持：stopped/running 态先清空断点表与待执行的单步/`monitor next`/`monitor finish` 状态，再回 `OK`、关闭连接并继续自由运行，此后不再监听、不接受重连，也不会因已设置的断点停止；终止态回 `OK` 后按最近一次运行的退出状态退出。扩展模式下 stopped/terminated 态收到 `D` 返回 `E01` 且不改变状态，running 态按 running 丢弃规则不应答；客户端用 `disconnect` 关闭连接，或用 `k`/`vKill` 处理终止态。`RXX` 成功时无应答，复位到入口并保留断点；仅扩展模式下可用，未启用扩展模式时回 `E01`；本次运行的帧、trap 记录、trace/stats 段清空，I/O 语义见 §6.7。`k` 无应答：stopped/running 态不改变程序状态（v1 不支持调试器主动杀死），终止态关闭连接并退出。`vKill` 在 stopped/running 态回 `E01`，终止态回 `OK` 并退出 |
| 退出 | `W<hh>` | `W<hh>` 只在自然终止或终止态出现：自然终止的执行报文、终止态下的执行报文与终止态下的 `?` 回 `W%02x`（0–255）；命中 TC trap 的执行报文先回 `T0b`（§6.7），之后的 `?`/执行报文按终止态回 `W<hh>`。`W` 只作为执行报文与 `?` 的停止应答；越界状态属实现错误（退出 71） |

- 调试期间的 stdin 读取必须可轮询、可恢复，并保留未形成完整 Token 的字节与解析状态。
  当前 `write`/`writeln` 的完整记录先按标准 §11.1 暂存；`--gdb` 唯一支持的 stdout 适配是
  `--gdb-stdout-journal=<dir>` 原子期刊。
- 期刊目录不得为空；最终组件以父目录 fd 上的 `openat(O_DIRECTORY|O_NOFOLLOW)` 验证，
  中间 symlink 按 §5.9 解析。symlink、非目录或形式非法为 2，访问/创建失败为 70。首指令前
  必须在目录内创建并非阻塞独占锁定 `.tc-gdb-stdout.lock`；已有锁也以 nofollow、非阻塞打开，
  `fstat` 确认普通文件后加锁，不得在 FIFO 或锁竞争上等待。完成私有临时项的 write/close/
  原子 no-replace rename/读取校验/清理探针；竞争、IO 或探针最终清理失败为 70。锁文件保留，
  进程退出时由宿主释放；探针只使用 `.tc-gdb-stdout.probe.tmp` 与 `.tc-gdb-stdout.probe.bin`。
- 每条记录分配永不复用的序号 `N=H+1`，文件名为 20 位零填充十进制
  `record-<N>.bin`。`.tc-gdb-stdout.next` 保存已预留高水位 `H`；先把 `H=N` 经私有临时项
  完整写入并原子替换提交，再用 `O_EXCL|O_NOFOLLOW` 临时项写记录，close 成功后以原子
  no-replace rename 提交最终名。rename 是该记录唯一提交点，其后不得再执行属于该记录的
  失败操作。rename 前失败只按身份清理临时项，最终记录不存在，已有记录不变，进程按
  `TC_RE_IO` trap 退出 1；高水位提交后即使失败也不复用 `N`。高水位临时项的创建/写入/
  close/rename 失败同样为 `TC_RE_IO`；若其临时项清理失败，本进程期刊进入永久失败态，
  `RXX` 不得复活，后续记录直接 `TC_RE_IO`，只能由进程重启后恢复。
- 宿主可见流精确定义为按数值序号拼接 `N<=H` 的最终记录；临时项、锁文件与高水位不属于
  可见流。查看器先原子读取 `H`，只枚举精确最终名并跳过缺口，不遍历到 `H`。`H=UINT64_MAX`
  表示耗尽；最终名已存在、高水位损坏或小于已有最大记录号均为 `TC_RE_IO`，不发布临时项。
- 每次获得锁后先恢复遗留 `.next.tmp`、`.probe.tmp`、`.probe.bin` 与 `.record-<N>.tmp`；
  只删除精确保留名且为 nofollow 普通文件，异常或清理失败为 70。高水位缺失时无记录则原子
  创建 0，否则取精确最终记录最大号；已有高水位不得小于最大记录号或被改小。零号、非 20 位
  名、symlink/非普通文件或不可解析高水位均为 70。
- 普通管道、终端与普通文件不构成期刊能力；`PIPE_BUF`、重试、truncate 或回滚不能替代
  rename 提交点。查看器自身 stdout 故障不改变 TC 的提交判定。
- 输出提交中途不得进入 stopped 或接受 `RXX`；`\x03` 记为待处理，直到 rename 成功或零可见
  失败后才停止。输入仍在完整 Token 后提交；EOF/非法 Token 触发 `TC_RE_IO`。`RXX` 不重置
  序号、不撤回已提交 I/O，保留未提交/预取输入并只复位其解析提交状态，从同一逻辑位置继续，
  不重读宿主；完全位于 TC 暂存区的整条输出可丢弃。

- 帧格式按协议：`$<payload>#<checksum>`；免应答模式之前，每个报文都需 `+`/`-` 应答；
  `RXX` 虽无 `$` 响应，ack 模式仍先发送 `+`，不得因无响应而漏 ack 导致客户端重发并重复
  复位。进入免应答模式后双方不得再发送或期待 `+`/`-`（GDB 对 `QStartNoAckMode` 的 `OK`
  可能发送最后一个 `+`，stub 忽略）。
- `PacketSize=4000` 是 RSP 十六进制值（0x4000=16384 个 data 字符），不含帧和校验和；
  stub 按入站 data 字符数 `16384`（另留 NUL/内部余量）实现。`qXfer` 的 `offset`/`length` 为
  十六进制；应答 `m<data>`/`l<data>` 的前缀各计 1 字符，有效 data ≤ `PacketSize-1`。
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
- 非免应答模式：校验和错误的报文以 `-` 请求重发；同一报文连续三次失败按 §6.1 处理：
  非终止态关闭连接、程序停在当前指令边界并回到监听；终止态按普通/扩展会话的断连规则
  退出或回到监听。免应答模式：不得发送 `-`，也不得期待
  `+`/`-`；校验和错误的帧不执行、不排队，静默丢弃并重新同步，成功解析一个完整帧后
  失败计数清零；连续三个坏帧同样按 §6.1 关闭连接。重连后回到默认 ack 模式
  （免应答只对当前连接有效，协议没有重新启用 ack 的报文）。

### 6.4 target.xml、架构与寄存器

RSP 的 `g`/`p` 寄存器编号由 `target.xml` 定义；`regnum` 只是 RSP 编号，不会把自定义
`pc` 自动绑定为 GDB 架构的 PC。因此 v1 不使用“四寄存器替代架构寄存器集”的描述：
`qXfer:features:read:target.xml:<offset>:<length>` 必须同时声明档案对应架构与完整核心
寄存器集，再追加 TC 抽象寄存器。GDB 一旦从描述看到寄存器，就不会采用架构默认布局；
缺失标准 feature 或标准寄存器的描述必须视为实现错误，不得连接后降级为四寄存器视图。

| profile | `<architecture>` | 标准 feature | 核心 raw 寄存器（编号连续） | 架构 `pc` 编号 | TC 编号 |
| ---- | ---- | ---- | ---- | ---- | ---- |
| `aarch64-linux-user`、`aarch64-baremetal` | `aarch64` | `org.gnu.gdb.aarch64.core` | `x0`–`x30` 0–30、`sp` 31、`pc` 32、`cpsr` 33 | 32 | 34–37 |
| `arm32-baremetal` | `arm` | `org.gnu.gdb.arm.m-profile` | `r0`–`r12` 0–12、`sp` 13、`lr` 14、`pc` 15、`xpsr` 16 | 15 | 17–20 |
| `riscv32-baremetal` | `riscv:rv32` | `org.gnu.gdb.riscv.cpu` | `x0`–`x31` 0–31、`pc` 32 | 32 | 33–36 |
| `w32` | `aarch64` | `org.gnu.gdb.aarch64.core` | 同 AArch64 行 | 32 | 34–37 |

- `w32` 没有具体目标机器，AArch64 描述只作为容纳 32 位合成调试值的 RSP 容器，不生成
  调试 ELF，也不表示 W=32 程序使用 AArch64 ABI。所有核心寄存器按 AArch64 位宽编码。
- 标准 feature 的位宽/类型必须被 GDB 识别：AArch64 `x0`–`x30`/`sp`/`pc` 为 64 位，
  `sp` 为 `data_ptr`、`pc` 为 `code_ptr`，`cpsr` 为 32 位自定义 `flags`；ARM M-profile
  核心全为 32 位，`sp` 为 `data_ptr`、`pc` 为 `code_ptr`；RISC-V `x0`–`x31`/`pc` 全为
  32 位，`pc` 为 `code_ptr`。不广告 FPU/VFP/SVE/NEON 等可选 feature。
- TC feature 固定为 `org.tc.ir`，四个寄存器均以 `regnum` 显式编号：

| TC 寄存器 | 位宽 | 含义 |
| ---- | ---- | ---- |
| `tc_ir_pc` | 64 | 下一条待执行 IR 指令的合成地址，或终止快照地址 |
| `tc_ir_depth` | 64 | 当前抽象调用深度 |
| `tc_ir_sp` | 64 | 当前帧在帧区内的偏移，不是架构栈指针 |
| `tc_ir_insn` | 64 | 已开始执行的 IR 指令数（含 trap 触发指令） |

- `target.xml` 的根元素为 `<target version="1.0">`，依次包含 `<architecture>`、上表对应的
  标准 feature 与 `org.tc.ir` feature。各 feature 内寄存器顺序、名称、位宽、类型与显式
  `regnum` 固定；AArch64 的 `cpsr` 使用 `<flags id="cpsr_flags" size="4">`，其中
  `<field name="mode" start="0" end="4"/>`，`cpsr` 本身为 32 位、`regnum="33"`、
  `type="cpsr_flags"`；四个 TC 寄存器均为 64 位 `uint64`。合成 `tc_ir_pc` 不使用
  `code_ptr`，因为 GDB 会把 32 位架构的预定义 `code_ptr` 缩为架构指针宽度，破坏
  64 位 RSP 布局；架构 `pc` 仍按上表使用 `code_ptr`。ARM/RISC-V 按同表生成。
  属性顺序与缩进不是跨实现字节契约；测试独立解析
  architecture、feature、flags/field、regnum、位宽与寄存器总数，不只比较 XML 字符串。

- `g` 按 `regnum` 升序返回全部 raw 寄存器，寄存器数据按目标端序、每字节两个十六进制
  数字，寄存器之间无分隔。AArch64/`w32` 固定 300 字节（600 hex），ARM M-profile 固定
  100 字节（200 hex），RISC-V 32 固定 164 字节（328 hex）。`p<n>` 的 `n` 按十六进制解析，
  只接受上表范围内的编号；越界、非十六进制或空编号返回 `E01`。
- VM 没有目标机寄存器状态。除架构 `pc` 与四个 TC 寄存器外，所有核心寄存器在 `g` 中
  返回与其位宽等长的 literal `x`，在 `p` 中返回等长 literal `x`，按 RSP 表示 unavailable；
  不得返回伪造的零值。架构 `pc` 在值可由该架构表示时镜像 `tc_ir_pc`；W=32 自然终止
  哨兵为 `2^32` 时，ARM/RISC-V 32 的架构 `pc` 返回 `xxxxxxxx`，64 位 `tc_ir_pc` 仍返回
  完整哨兵；`w32` 的 64 位架构 `pc` 返回 `0000000100000000`。
- `P` 默认返回 `E01`；`--gdb-allow-write` 下仅允许在非终止态写架构 `pc`，值按该架构
  位宽解析，并转换为 64 位合成地址；必须落在 §6.2 的有效指令边界，否则 `E01`。
  写 `tc_ir_pc`、其它架构寄存器或 TC 寄存器均返回 `E01`。`G` 不支持，返回 `E01`。
- 终止态（自然终止或 trap 终止）四个 TC 寄存器仍按固定 16 hex/寄存器返回：

| 状态 | `tc_ir_pc` | `tc_ir_depth` | `tc_ir_sp` | `tc_ir_insn` |
| ---- | ---- | ---- | ---- | ---- |
| 自然终止 | `TC_DEBUG_CODE_BASE + 4*N`（终止哨兵，不是有效指令地址；`N` 是全局指令数） | 0 | 0 | 已开始执行的 IR 指令数 `E` |
| trap 终止 | 触发 trap 的指令地址 | 触发帧的抽象深度 | 触发帧在帧区内的偏移 | 含 trap 指令在内的已开始执行 IR 指令数 |

- 自然终止的 `tc_ir_pc` 哨兵不满足 §6.2 的有效指令地址；终止态下 `Z0`、不带地址的
  `monitor ir` 对它返回 `E01`。终止态下所有 `P`（包括写架构 `pc`）均返回 `E01`。
  trap 终止保留触发帧，`monitor trap`/`frames`/`vreg` 按该帧读取。
- 终止态不采用 GDB 的 `x` 作为 TC 寄存器值；四个 TC 寄存器始终按本节固定值返回，
  只有无法装入架构 `pc` 的 W=32 哨兵按上一规则返回 unavailable。
- 现有档案均为小端。64 位寄存器固定 16 hex，32 位寄存器固定 8 hex；W=32 的 TC 寄存器
  仍为 64 位。有效 `tc_ir_pc` 的取值受 §6.2 的 32 位合成区间约束，自然终止哨兵在上界
  取 `0x00000001_00000000` 时按 64 位字段编码。
- `P` 的值字段严格使用架构 `pc` 位宽：AArch64/`w32` 为 16 hex，ARM/RISC-V 32 为
  8 hex。`Z0`/`z0` 与 `m`/`M` 的地址字段接受 1 至该地址宽度对应的十六进制位，不要求
  前导零补齐；AArch64/`w32` 至多 16 位，ARM/RISC-V 32 至多 8 位，空、非十六进制或
  溢出返回 `E01`。这是 RSP 数值字段，与 `monitor ir` 的固定字面格式不同。
- 不暴露逐个 vreg 寄存器：IR 没有目标机寄存器分配，暴露伪寄存器会误导调试者；
  vreg 通过 `monitor vreg` 读取。
- 目标描述随 profile 固定，不随调试 ELF 是否存在而改变；ELF 的 class/machine 必须与
  描述一致。描述与 ELF 不一致时连接失败属实现错误，不得静默换用另一套编号。

### 6.5 内存视图

- `qXfer:memory-map:read` 返回五区（帧/静态/设备/堆/代码）基址与长度，来自当前平台档案。
  XML 结构固定为 `<memory-map>` 下按 frame、static、device、heap、code 顺序排列五个
  `<memory>` 元素；frame/static/heap 为 `type="ram"`，device/code 为 `type="rom"`，
  `start`/`length` 使用档案的十六进制地址值。元素不写区名或 `property`，合成代码区不出现。

- XML 不使用官方 DTD 未允许的 `property`；`property name="blocksize"` 只属于
  `type="flash"`，本映射不使用。
- `m` 读取不触发程序语义：直接读取区镜像，不消费设备输入、不改变指针状态、不产生 trap；
  五区与合成代码区之外的地址返回 `E01`；跨出所在区边界的整次读取同样返回 `E01`，
  不做部分读取。
- 代码区在 VM 中只承载函数地址语义，没有指令字节；读取返回全零，函数定位用
  `monitor funcs`。
- 合成代码区（§6.2）不在内存映射中：它不是程序可访问区，断点由 stub 直接处理。
  GDB 假定未映射区是 RAM，因此该缺席不构成访问限制；`m` 按 §6.2 读取并返回零字节，
  `Z0` 按 §6.7 接受有效指令地址，`M` 按本节写禁区返回 `E01`。
- 设备区默认可读但不可写；`--gdb-allow-write` 也不放开设备区写。
- 写帧/静态/堆区可能破坏语言不变量；checked build 在后续访问处按既有规则 trap，
  调试器会话不因此改变必选 trap 集合。

### 6.6 监视命令

`qRcmd` 子集，输出为十六进制编码文本：

| 命令 | 内容 |
| ---- | ---- |
| `monitor regions` | 五区基址/长度/当前使用量（字节） |
| `monitor funcs` | 有指令函数表：`module`（根相对显示模块路径）、源码函数名、`funcaddr` 值（按区名+区内偏移）、指令区间（全局指令序号）、合成地址范围；顶层单元不是函数，不列入 |
| `monitor objects` | 对象表：对象 id、区、偏移（字节）、大小（字节）、生命期状态；对象 id 是内部标识，不承诺跨构建稳定 |
| `monitor frames` | 抽象帧栈：深度、实体 `kind`（`unit`/`function`）、`module`、`func`（顶层 `<top>`，函数源码名）、全局指令序号（§6.2）、源码位置 |
| `monitor next` | 执行到下一条同深度指令，跨越整个 `call`/`call_indirect`；途中命中用户断点、TC trap、终止或 `\x03` 时提前停止 |
| `monitor finish` | 运行到当前帧返回（深度减一）；途中命中用户断点、TC trap、终止或 `\x03` 时提前停止 |
| `monitor ir [<addr>]` | 指定地址或当前 pc 的 IR 文本（含操作数与 span；可能含 `%N`/`f<id>`/`struct#id`/label 等结构操作数，不是调试实体身份）；`<addr>` 必须是 `0x` 前缀的 1–16 位 ASCII 十六进制字面量（大小写数字均可，无符号/空白/下划线；不接受 `0X`、裸十六进制或十进制），按 64 位无符号解析；W=32 时高 32 位必须为 0 且落在 32 位合成区间；地址越界、未对齐、不对应指令或字面非法返回 `E01`；省略时使用当前 pc |
| `monitor vreg [<selector>:]<序号>` | 读取当前帧的 vreg 值：标量按 vreg 自身类型的位宽打印为 `0x` 十六进制位模式，宽度取 `tc_type_bits(type, W)`：`int8`/`uint8`/`bool` 2 个十六进制数字、`int16`/`uint16` 4 个、`int32`/`uint32`/`float32` 8 个、`int64`/`uint64`/`float64` 16 个，`isize`/`usize` 随档案字长（W=32 为 8 个、W=64 为 16 个）；指针/fnptr 打印为区名+区内偏移（空指针为 `null`，不可表示时按 W 位模式）；聚合与未初始化 vreg 返回 `E01`。`<序号>` 为 vreg 序号的十进制字符串，即 `monitor ir` 显示的 `%N`（IR 操作数不携带源名）；省略选择器时在当前实体的 vreg 空间内解析，未找到返回 `E01` |
| `monitor stats` | §5.5 的 `tc-vm` 运行时字段，每行 `key=value`，不含 `# tc-stats` 头；返回自本次 `RXX` 以来到当前停止点的即时累计快照，重复调用不改变计数；仅在实际 trap 后追加 `first_trap` |
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
- `monitor funcs` 只列至少含一条 IR 指令的函数；没有指令的函数（如空 `void` 函数）
  不进入调试函数表，不虚构调试器 `funcaddr` 或合成地址范围。这只排除调试器合成视图，
  不改变标准 §8.5/§8.8.4 的语言级 `funcaddr`：空函数仍可有合法、互不相同的函数入口，
  直接调用或 `call_indirect` 空函数仍按标准立即返回。`const`/`static var`/`struct` 不是函数，
  不进入函数表。若未来增加
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
  四个 TC 寄存器的终止快照见 §6.4。
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

- 断点地址必须是 §6.2 的有效指令地址；越界、未对齐或不对应指令为 `E01`。`s` 执行一条
  IR 指令后停在下一边界，分支没有特殊单步规则；`call`/`call_indirect` 作为原子指令，被调
  函数有指令时停在其首条，空函数立即返回调用者下一边界。`c` 运行到断点、trap、终止或
  `\x03`，I/O 挂起/提交规则按 §6.3。
- 断点命中时 pc 停在尚未执行的断点指令；下一次 `c` 先静默执行该地址一条指令，再从新 pc
  正常检查，避免零进展。包括入口预置断点的首次 `c`；step-over 中 trap 报 `T0b`，`\x03` 完成后
  报 `T02`。`s` 不做 step-over。跨越调用与返回使用 `monitor next`/`finish`，gdb 自身 next/
  finish 不作为验收项。
- TC trap 按标准 §12.5.2 完成提交后停止并报 `T0bthread:1;`，程序进入终止态；`monitor trap`
  读取详情。`T0b` 只作执行报文（`c`/`s`/`C`/`S`/`vCont`）的直接停止应答，`?` 随后报 `W<hh>`。
  终止态执行报文不再推进，直接回 `W<hh>`；退出状态与无调试器一致。普通/扩展会话的终止后
  生命周期按 §6.1/§6.3。
- `RXX` 清空本次帧、trap 与统计/trace 状态，按冻结 IR 重新准备：可达库 `static var` 按 const
  重建，程序变量恢复未声明/未初始化并在声明时初始化，帧区、对象表、指针状态、分配器/堆
  与设备恢复进程启动初态；CT 与冻结 IR 不重算。已提交 I/O 不回退，未提交/预取输入保留且
  只复位解析提交状态，完全暂存的整条输出可丢弃。旧 trace 段不补 footer，保留 truncated 并
  视为 abandoned；新段重新输出 header（`seq=1`）、footer 与 trap/stats，同一 sink append-only，
  会话行只在启动时输出一次。
- 暂停期间不推进、不超时、不提交 I/O。断点增删在 stopped/terminated 态立即生效，后续
  `c`/`monitor next`/`finish` 使用最新表；running 只按 §6.3 处理，陈旧断点不触发。普通
  `target remote` 的 `D` 清空断点与待执行命令状态后自由运行，此后不再监听或停止。

### 6.8 与 AOT 调试视图的一致性

- AOT 与 VM 共用 §5.10 的行表生成器；同一源程序的 `file:line:col → 指令` 映射一致。
  调试 ELF 的 `.file` 使用 §5.9 显示路径；gdb 从工程根启动；不写绝对 `DW_AT_comp_dir`。
- VM 可选生成**调试 ELF**（`--gdb-elf-out=<path>`）：由 `tc-vm` 内部最小 ELF writer 直接
  生成，只承载符号与行表，地址使用合成代码地址，不含可执行代码；不调用 clang、lld、
  gdb、readelf 或其它外部工具。调试 ELF 的函数符号使用源码函数名（即 `func`），不追加
  `#<func_id>`；不同模块同名函数由 DWARF `file:line` 区分，`break <name>` 不保证唯一，
  源级断点用 `break <file>:<line>`。顶层单元不是函数符号，不写入 `STT_FUNC`、入口地址或
  `<top>` 合成身份；但只要顶层单元含 IR 指令，其 `(file,line,col)` 行必须写入
  `.debug_line`，使顶层指令与 §5.10 linemap 全序一致。TC 调试身份元组 `(module,func)`
  不作为合成键写入 `.symtab`/DWARF，`kind`/`<top>` 不写入；模块显示路径通过 DWARF
  `decl_file`/行表体现。只有至少含一条 IR 指令的函数写入函数符号与对应行表；没有指令的
  函数（如空 `void` 函数）不虚构调试入口地址，也不写入符号、入口地址或对应行表项；该
  限制不改变语言级 `funcaddr` 与间接调用语义。gdb 以 `file <elf>` + `target remote` 获得
  源级断点时，断点地址与 stub 的合成地址一致；含指令的单个 `top` 函数仍可用 `break top`
  下源级断点，空函数不承诺 `break top`。
- `tc-aot --debug --emit=elf` 的 ELF 符号仍是后端内部 `tc_main`/`tc_function_<func_id>`；
  AOT 的 gdb 调试按 DWARF `file:line`，不承诺 `break <源码函数名>`。本节源级符号命名只
  针对 VM `--gdb-elf-out` 调试 ELF。
- VM 调试 ELF 的 ELF class/machine/endianness 固定如下表；`e_type`、节区顺序、具体最小
  节集（`.symtab`/`.strtab`/`.shstrtab`/`.debug_line`，以及 gdb 所需的 DWARF v4 节）
  与文件内地址布局属于内部 writer 的实现选择，不构成跨实现字节契约；调试地址仍按 §6.2
  的合成视图。P4b 只使用独立语义解码与真实 gdb 验证产物可加载、符号/行表正确且断点地址
  与 §6.2 一致，验证结果写入 §6.10 证据，不回写本节。目标文件格式只服务于 gdb 加载符号，
  不用于执行、反汇编或分发；不得建立跨实现 byte-exact golden。绑定当前实现的固定字节
  golden 仅作回归证据，不得取代真实 gdb 验收或提升为规范。

| profile | ELF class | data | e_machine | DWARF |
| ---- | ---- | ---- | ---- | ---- |
| `aarch64-linux-user`、`aarch64-baremetal` | `ELFCLASS64` | `ELFDATA2LSB` | `EM_AARCH64` (183) | 4 |
| `arm32-baremetal` | `ELFCLASS32` | `ELFDATA2LSB` | `EM_ARM` (40) | 4 |
| `riscv32-baremetal` | `ELFCLASS32` | `ELFDATA2LSB` | `EM_RISCV` (243) | 4 |

- `w32` 是 target-independent VM static profile，没有具体 `e_machine`；`w32` 的
  `--gdb-elf-out` 退出 70（profile 能力不可用），不生成调试 ELF。`w32` 的 `--gdb` 仍可用
  `target.xml`（固定使用 §6.4 的 AArch64 RSP 容器），但不提供
  源级符号 ELF。
- VM 调试 ELF 与 AOT `.loc` 共用 §5.10 的行表生成器映射，但**不**调用 AOT 的 clang/lld/
  assembler/linker 管线；两边的 `file:line → 指令` 映射一致由共享行表测试覆盖。
- `--gdb-elf-out` 可与 `--gdb` 同用，也可单独使用（生成 ELF 后正常执行），便于先产出
  调试 ELF 再启动会话；与 `--static-only` 同用时只生成 ELF、不执行（§5.2.1）。
- 调试 ELF 是可选增量，不阻塞 T3 验收；无调试 ELF 时 gdb 仍可按合成地址下断点。

### 6.9 安全与语义约束

- stub 默认只读程序状态；`--gdb-allow-write` 下可写 `pc` 与帧/静态/堆区（§6.5），
  其余写操作返回 `E01`。
- 被动会话（不写寄存器/内存、不复位）下，stub 不得改变程序逻辑 stdout 的字节、trap 选择与
  退出状态；同一程序有/无调试器运行时，普通 stdout 与 §6.3 期刊已提交记录按序拼接的流
  必须字节一致。`--gdb` 本身不向 fd1 写程序输出。
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

### 6.10 测试与证据

- `vm.gdb.protocol` 使用仓库内 `tests/tools/vm_gdb_client.py`，覆盖 §6.1–§6.7 的报文、
  寄存器/内存、monitor、断点/单步、断连重连、`RXX`、输入恢复与原子期刊故障注入；不依赖
  真实 gdb，进入常规 `ctest`。`vm.gdb.semantics` 比较无调试器、仅 continue、多次暂停后
  continue 三种场景的逻辑 stdout（期刊已提交记录拼接）、首个 trap 与退出状态。
- 真实 gdb 端到端由固定 Linux CI 作业执行 `tests/tools/vm_gdb_e2e.py`、`vm.gdb.e2e`、
  `toolchain.aot_debug` 与 `toolchain.debug_smoke`（全部适用 profile）。本地缺 gdb 可显式
  跳过并记录原因，但不得记为 PASS；LLDB 只作机会性验证。
- CI 保存 `build/gdb-e2e/result.json`、`transcript.log` 及平台 provenance 对两文件生成的
  签名 attestation。结果至少包含 `schema_version`、`status="PASS"`、提交 SHA、容器 digest、
  `gdb --version` 原文、profile、测试套件与用例名（含 AOT/qemu gdbstub）、非 SKIP 断言数。
  字段缺失、SKIP、超时或失败均不构成 PASS。
- G11 审计运行
  `tests/tools/s10_release_audit.py --check --require-g11-evidence=<artifact-dir>`，并用
  `gh attestation verify <artifact>` 校验签名、仓库与 `.github/workflows/ci.yml` 固定作业身份；
  subject digest 必须匹配 artifact 字节，自报字段与 provenance 不一致也失败。普通本地
  `--check` 不要求该 artifact。

最小使用方式为 `tc-vm --gdb --gdb-stdout-journal=<dir> demo.tc`，再从另一终端以
`gdb -q` 执行 `file <debug-elf>` 与 `target remote 127.0.0.1:<port>`；restart 使用
`target extended-remote`。stdout 由 `tests/tools/vm_gdb_stdout_adapter.py` 按序号读取已提交
记录；普通 shell 管道或终端不能替代期刊。

## 7. 验收与门禁

§5–§6 的每条规范都须由测试覆盖正常路径、反例、错误优先级和输出为空/部分提交时的结果。
测试描述不得取代或改写契约；每个里程碑的复现命令写入 `docs/gates/`，记录保持一页以内。

### 7.1 测试索引

下表是 0.0.47 交付时的测试注册映射；尚未落地的入口在实现并注册前不得作为当前仓库的
PASS 证据。

| 契约 | 测试入口 |
| ---- | ---- |
| 工程模型、三入口 CLI、manifest、init/test、路径与输出身份 | 见[TC 工程模型 0.0.47](TC工程模型-0.0.47.md) §9.1；`toolchain.cli_matrix`、`toolchain.output_aliases`、`toolchain.project_repro` |
| IR dump、trace、stats、诊断 JSON、linemap（§5.3–§5.7、§5.10） | `toolchain.ir_dump`、`toolchain.trace_golden`、`toolchain.stats`、`toolchain.diag_json`、`toolchain.linemap` |
| AOT 调试与代码生成不变量（§5.8） | `toolchain.aot_debug`、`toolchain.debug_smoke`、`toolchain.codegen_invariance` |
| 调试实体、符号与成员边界（§5.4、§6.4、§6.6、§6.8） | `toolchain.debug_names`、`toolchain.member_identity`、`toolchain.debug_elf` |
| RSP 协议、寄存器/内存、monitor、复位与调试 I/O（§6.1–§6.7） | `vm.gdb.protocol`、`vm.gdb.semantics`、`vm.gdb.e2e` |
| reducer（§5.12） | `toolchain.reduce` |
| 真实 gdb 证据与发布审计（§6.10、§7.2） | `toolchain.g11_evidence` |
| CMake presets 与测试子集（§7.2） | `toolchain.presets` |

标签集合固定为 `unit`、`conformance`、`toolchain`、`aot`、`slow`、`fast`；每个测试至少一个
标签，`fast` 与 `slow` 互斥。`fast` 不依赖 gdb/qemu/clang/lld，且不含 scale/longterm/slow
测试；它不承诺固定时长。外部依赖缺失时按既有策略配置失败或显式跳过，不得静默通过。

### 7.2 0.0.47 退出标准

- T0–T6 与工程模型 P0–P5 全部完成，G11 PASS，`docs/gates/README.md` 更新。P4b 必须
  有 §6.10 固定 Linux CI 作业产生的真实 gdb PASS artifact；本地跳过不计为完成。
- G11 证据按 §6.10 验收；证据缺失、SKIP、字段不完整、签名或来源校验失败均不构成 PASS。
- 新增测试全部进入 `ctest`，`fast` 子集在 CI 上通过；既有 `ctest`、golden、确定性、
  `docs.cleanup` 与发布审计零回归。语言差分仍只比较 stdout、首个 trap 与退出状态。
- 三入口 `--version` 打印工具链 0.0.47 与语言 0.0.46；契约检查仍断言 51 opcode，release
  audit 覆盖三个入口。
- 文档同步范围为两份 0.0.47 设计、README/README.en、docs map、门禁、AGENTS 导航、
  tests/src README、`src/CONTRACTS.md` 与 CHANGELOG；冻结的 0.0.46 标准不改。

## 8. 实现约束与开放限制

- 共享行表、退出码映射与路径显示分别以 §5.10/§5.1/§5.9 为准；无观察者语义以 §6.9
  为准，零额外分配以 §4 的 T3 验收为准。实现必须复用同一权威模块或映射，不得建立
  并行定义。
- 身份与输出安全以工程模型 §4/§6 及本文件 §5.1/§5.12/§6.3 为准。系统调用可以替换，
  但错误优先级、零可见失败与 no-replace 提交语义不得改变；canonical 字符串不得作为
  安全边界。
- **P4b 实现验证项（非开放规范）**：VM 调试 ELF 的 `e_type`、节顺序、最小节集与文件内
  地址布局按 §6.8 作为内部实现选择处理；P4b 只验证并提交 §6.10 证据，不回写规范，不
  建立跨实现字节 golden；该项不阻塞 T3。验证失败只修实现，不得放宽 §6.8 的 class/
  machine/endianness、DWARF v4、符号或行表语义，也不改用外部 assembler/linker。
- T3 前的兼容性验证覆盖：各 profile 的 target.xml 与架构 pc 绑定、`g`/`p` 布局、qXfer/
  no-ack/vCont/扩展 restart、`T0b` 后的停止/退出状态，以及 LLDB 的机会性兼容结果。
  P4b 额外验证 VM 调试 ELF 与 `target remote` 的源级加载和断点。验证失败按对应契约
  修复，不以跳过替代 PASS。

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

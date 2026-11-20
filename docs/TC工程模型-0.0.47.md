# TC 工程模型 0.0.47

## 0. 依据与范围

- 语言语义以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为唯一权威；本文件
  不修改语言标准（51 个 opcode、80 个诊断码、零 pass 基线与 ABI 冻结）。
- 本文件定义 0.0.47 的**工程模型**：工程布局、`tc.project`、根锚点、路径规则、`tc` 工具与
  工程内测试。它是工具链的环境与项目约定，不构成语言可观察行为。
- 0.0.47 的工程模型按清洁设计：**不保留"绝对路径输出"模式**，也不承诺与 0.0.46 的
  工具文本/产物路径字节一致；程序语义、ABI 与零 pass 基线不变。
- 编译器入口仍是 `tc-vm` 与 `tc-aot`；`tc` 是 0.0.47 新增的工程工具，不编译程序。
  工程模型与调试/可观测设计的关系见
  [TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md)。

## 1. 工具与契约

| 工具 | 角色 | 是否编译 |
| ---- | ---- | ---- |
| `tc` | 工程初始化、工程测试、工程元数据 | 否 |
| `tc-vm` | 参考 VM，执行最终 IR | 否 |
| `tc-aot` | 原生后端，生成 asm/obj/elf/flat | 是 |

- 契约表述从"两个 CLI"改为"**两个编译器入口 + 一个工程工具**"；`tests/tools/
  s10_release_audit.py`、`src/driver/CMakeLists.txt`、AGENTS 与 README 的入口清单必须同步。
- `tc` 与编译器共享退出码：`0` 成功、`1` 语言诊断/测试失败、`2` 用法错误、`70`
  环境不可用、`71` 内部错误；不新增退出码。
- `tc --version` 打印工具链与语言版本，例如
  `tc 0.0.47 (language 0.0.46; project tool)`。
- `tc` 不提供公共 C 接口、嵌入 API 或构建脚本执行能力。

## 2. 工程布局

初始化后的工程：

```text
.
├── tc.project
├── src/
│   ├── main.tc              # entry，按 #program 编写
│   └── lib/                 # 可选：#lib 模块
├── tests/
│   ├── smoke/
│   │   ├── main.tc          # 测试入口（#program）
│   │   └── stdout.bin       # 期望 stdout 精确字节
│   └── exit/
│       ├── main.tc
│       ├── trap             # 期望 "TC_RE_*"
│       └── exit             # 期望退出码（十进制文本）
├── build/                   # 生成物；.gitignore 忽略
└── .gitignore
```

约定：

- `src/` 是源文件根；`entry` 默认 `src/main.tc`。
- `tests/<case>/main.tc` 是测试入口；测试不与 `src/` 混放，不进入发布构建。
- `build/` 是 out-of-source 生成目录；`tc --test` 不向源目录写生成物。
- 工程内测试发现固定为**一层**：`tests/*/main.tc`；排序后执行，测试名为目录名。
- `profiles` 以 `tc.project` 的 `profile` 为准；测试与构建共用该默认值。

## 3. `tc.project`

### 3.1 语法

行式、声明式、无代码执行；`#` 起始为注释，空行忽略：

```text
# tc.project
tc-project 1
name = "demo"
entry = "src/main.tc"
include = "src"
profile = "aarch64-linux-user"
```

- 第一条非注释行必须是 `tc-project <version>`；当前只接受 `1`。
- 字符串用双引号；支持 `\"` 与 `\\` 转义；不支持多行字符串。
- 未知版本、未知键、重复的单值键、格式错误 → 退出 2，不静默忽略。
- 工程文件中的路径必须是相对路径；绝对路径 → 退出 2。

### 3.2 字段

| 字段 | 必需 | 说明 |
| ---- | ---- | ---- |
| `tc-project` | 是 | 格式版本；当前为 `1` |
| `name` | 否 | 工程名；缺省取工程根目录名。初始化工具写入命令行给的名称 |
| `entry` | 否 | 默认入口，缺省 `src/main.tc`；保留给未来的 `tc --build/--run`；0.0.47 的编译器仍接收显式 source |
| `include` | 否 | 可重复、有序的 import 根；相对工程根；缺省为 `src` |
| `profile` | 否 | 默认档案；缺省 `aarch64-linux-user`；必须是已实现档案；编译器 CLI 的 `--profile` 覆盖它 |

- CLI `--include` 根排在 manifest 的 `include` 之后（manifest 顺序优先，CLI 作为增量）。
- `include` 允许 `..`；根外文件进入"外部文件"路径规则（§4.3）。
- `entry` 在 0.0.47 只做元数据；`tc-vm`/`tc-aot` 仍以命令行 source 为准。

## 4. 根锚点与路径

### 4.1 根发现

- 编译器（`tc-vm`/`tc-aot`）：从 **root 模块 canonical 路径**向上找最近的 `tc.project`；
  工程根 = manifest 所在目录。找不到 manifest 时，工程根 = root 模块所在目录。
- `tc --test`：从当前目录向上找最近的 `tc.project`；找不到 → 退出 2（工程测试必须在工程内）。
- 嵌套工程允许：最近祖先生效；`tc --init-project` 可在已有工程内创建子工程（子工程有
  自己的 `tc.project`）。这是"每个工程一个锚点"，不是"整个文件树一个锚点"。
- 根路径与源路径都先 canonical 化；symlink 不改变显示结果。

### 4.2 唯一锚点

- 所有输出路径都相对**工程根**，不相对 root 模块目录、不相对 include 根、不使用绝对路径。
- 内部 `canonical_path` 只用于模块实例键、去重、import 解析；**永不输出**。
- 显示路径 = root 到文件的词法相对路径；根内文件形如 `src/main.tc`、
  `tests/smoke/main.tc`。
- 根外文件（import/include 指到根外）：使用 `../…` 相对路径，并在行表条目标
  `"external":true`；不同卷或不可表示的路径回退 `<basename>#<n>`（按 IR 全序首次出现编号）。

### 4.3 路径消费者

以下输出一律使用显示路径：

- 文本诊断、相关位置、LEX 非法字节 note；诊断 JSON。
- VM trap 文本与 JSON trap 行。
- `--dump-ir` 的 `modules:` 段。
- trace 的 `span` 与 header `source`；linemap 的 `source`/`file`。
- 调试 ELF 的 `.file` 与符号所在源文件；AOT `.file`/`.loc`/`# source:`。
- AOT 产物内的源文件字符串（`.asciz`）与目标运行时 trap 记录。
- `monitor frames`/`ir`/`funcs` 的源码位置。
- `tc --test` 的报告与失败诊断。

无 `--trace-paths=abs`、`--debug-path-map` 一类绝对路径开关；0.0.47 不提供绝对输出模式。
外部工具（clang/lld）自身打印的绝对临时路径不属于 TC 输出契约，属于外部工具诊断。

### 4.4 调试信息

- `tc-aot --debug`：`.file <fileno> "<显示路径>"`、`.loc <fileno> <line> <column>`；`.loc`
  的第一个操作数是文件编号，不是路径。
- 调用 clang 时固定 `-fdebug-compilation-dir=.`，不把绝对工程根写入 `DW_AT_comp_dir`。
- gdb 默认从工程根启动，源码路径按根相对解析；从其它目录调试时需要 `directory` 设置。
- 行表覆盖 `linemap` 全序；`.debug_line` 允许合并相邻同 `(file,line,column)` 行，不按行数
  验收。
- AOT `.asciz` 与目标 trap 使用显示路径；VM 与板端 trap 的路径文本一致。

## 5. `tc --init-project`

```sh
cd project_dir
tc --init-project project_name
```

- 当前目录就是工程根；不创建 `project_name/` 子目录；`project_name` 只写入 `name`。
- `project_name`：非空；拒绝 `/`、`\`、控制字符、`.`、`..`、`"`；允许空格与普通 UTF-8。
- 当前目录已有 `tc.project` → 退出 2。
- 当前目录非空 → 允许；只创建缺失文件，不覆盖已有 `src/main.tc`、`tests/...`、`.gitignore`；
  已存在的项打印 `skipped`。
- 生成：
  - `tc.project`：`tc-project 1`、`name`、`entry = "src/main.tc"`、`include = "src"`、
    `profile = "aarch64-linux-user"`；
  - `src/main.tc`：最小合法 `#program` 模板（`_ = writeln int32, 1`）；
  - `tests/smoke/main.tc` 与 `tests/smoke/stdout.bin`：与模板一致的 smoke 测试
    （stdout 字节为 `31 0a`，即 `1\n`）；
  - `.gitignore`：`build/`。
- 不创建 `build/`，不调用编译器，不写任何绝对路径。
- 输出：stdout 打印 `created`/`skipped` 的**相对路径**，最后打印 `next: tc --test`；不含
  cwd 绝对路径。
- 退出码：0 成功（含 skipped）；2 已有工程/名称非法；70 IO/环境；71 内部。
- 写入失败：删除本次新建的文件，保留原有文件，退出 70。
- cwd 为 symlink：canonical 化后写目标目录。

示例：

```text
$ tc --init-project demo
initialized project "demo"
created tc.project
created src/main.tc
created tests/smoke/main.tc
created tests/smoke/stdout.bin
created .gitignore
next: tc --test
```

## 6. `tc --test`

### 6.1 发现

- 仅发现 `tests/*/main.tc`（一层）；非目录、无 `main.tc` 的目录忽略；按目录名排序。
- 测试名 = 目录名；`tc --test` 无参数执行全部；带参数按测试名筛选；未知名称 → 退出 2。
- `tests/` 不存在 → 0 个测试，退出 0，打印 `0 tests`。

### 6.2 侧车文件

| 文件 | 含义 | 缺省 |
| ---- | ---- | ---- |
| `stdout.bin` | 期望 stdout 精确字节 | 空 |
| `stdin.bin` | 喂给程序的 stdin 字节 | 空 |
| `trap` | 期望首个 `TC_RE_*`（文本，可带行尾换行） | 无 trap |
| `exit` | 期望退出码（十进制文本） | 有 `trap` 时为档案 trap 状态（当前 1），否则 0 |

- `stdout.bin`/`stdin.bin` 是二进制精确字节；不使用文本换行转换。
- `trap` 缺省表示期望正常终止；`exit` 显式给出时必须与运行结果一致。
- 测试目录本身是 include 根（优先于 manifest 的 `include`），再叠加 manifest 的 include 顺序。
- 测试入口必须能通过静态阶段；编译诊断按普通诊断格式输出，测试计为失败。

### 6.3 执行

- 每个测试独立执行：全新的 frontend/IR/VM 状态；cwd = 工程根；profile = manifest 的
  `profile`；不调用 AOT/clang/lld。
- `tc` 直接复用私有 core 的加载与 `tc_vm_run`，不 exec `tc-vm`；不导出公共 API。
- 单测试超时 30 秒；超时计为失败（退出 1），不挂死。
- 实际输出与期望逐字节比较；失败时打印测试名、stdout 差异、trap/exit 差异；全部路径
  root 相对，不含绝对路径。
- 不生成 build 产物；不向 `tests/` 写文件。

### 6.4 输出与退出码

```text
project "demo" (aarch64-linux-user)
ok   tests/smoke
FAIL tests/exit: trap expected TC_RE_DIV_ZERO, got none
1 passed, 1 failed
```

- 成功：0；有失败：1；用法/无工程/未知测试名：2；环境或 IO：70；内部：71。
- 报告与汇总写 stdout；编译诊断与失败详情写 stderr；两者都没有绝对路径、颜色或时间戳；
  顺序按测试名排序。

## 7. `tc` CLI

```sh
tc --init-project project_name
tc --test [test_name...]
tc --help
tc --version
```

- `tc` 不编译、不运行 entry；编译仍由 `tc-vm`/`tc-aot` 完成。
- `--init-project` 与 `--test` 不能同时出现；未知参数/多余参数 → 2。
- 未来 `--build`/`--run` 若加入，复用 `entry`/`profile`；0.0.47 不实现。

## 8. 实现落点

| 模块 | 内容 |
| ---- | ---- |
| `src/driver/tc.c` | `tc` 入口、`--init-project`/`--test`/`--help`/`--version`、退出码 |
| `src/project/manifest.[ch]` | `tc.project` 解析、根发现、字段校验、默认值 |
| `src/project/init.[ch]` | 模板与原子写入、名称校验、skipped 报告 |
| `src/project/test_run.[ch]` | 测试发现、侧车读取、私有 core 驱动、比较与报告 |
| `src/base/path.[ch]` | root/canonical/display、外部 `../`、`#<n>`、路径一致性 |
| 前端 | `TcSpan` 增加 display；`TcModuleInfo`/`TcParseModule`/`TcModule` 增加 `display_path`；module loader 传 display；sem 诊断用 display |
| 编译器驱动 | `tc-vm`/`tc-aot` 读 manifest、确定根、`--include` 顺序、错误码 |
| 后端 | 三后端统一 `tc_span_display()`；`.asciz`、`.file`/`.loc`、`# source:` |
| VM/调试 | trap、trace、linemap、debug ELF、`monitor` 用 display |
| 构建 | `CMakeLists.txt` 新 `tc` target、project/path 模块；release audit 三入口 |

- `tc` 链接私有 `tc_core`；不新增公共 C 库、不安装头文件。
- `tc.project` 解析器不依赖外部 TOML/JSON 库；错误信息包含行号，路径 root 相对。
- 所有路径输出共享同一 `tc_path_display`，禁止各后端/工具自行拼路径。

## 9. 验收与测试

### 9.1 仓库测试位置

| 内容 | 位置 | 标签 |
| ---- | ---- | ---- |
| `tc` CLI：init/version/help/退出码 | `tests/project/tc_cli_test.py` | `toolchain`, `fast` |
| manifest：语法、默认值、根发现、非法项 | `tests/project/manifest_test.py` | `toolchain`, `fast` |
| 工程测试 runner：侧车、过滤、失败、超时 | `tests/project/test_runner_test.py` | `toolchain`, `fast` |
| 路径：诊断/trap/IR/trace/linemap/AOT 字符串 | `tests/project/path_test.py` | `toolchain` |
| 跨根可复现 asm/ELF/flat | `tests/determinism/project_determinism.py` | `toolchain`, `slow` |
| 三入口/release audit | `tests/tools/s10_release_audit.py`、`tests/tools/cli_contract_test.py` | `toolchain` |
| harness adapter 的 root 解析 | `tests/adapters/`、`tests/harness/` | `conformance` |

### 9.2 验收

- `tc --init-project` 在临时目录生成完整工程；生成文件无绝对路径；smoke 通过 `tc --test`。
- 已有 `tc.project`、非法名称、非空目录、已有文件、symlink、权限失败均有确定行为。
- manifest 的版本/未知键/重复键/绝对路径/非法 profile 全部退出 2；include 顺序确定。
- 从工程根、子目录、工程外调用 `tc-vm`/`tc-aot`，诊断/trap/IR/trace/linemap 路径一致且
  根相对；无 `/Users/`、`/home/`、`/private/` 等绝对路径。
- 两个不同根目录构建同一工程，asm/ELF/flat 逐字节一致；代码/ABI 语义不变。
- `tc --test`：stdout/stdin 精确字节、trap/exit 侧车、失败报告、过滤、无测试、超时。
- gdb 从工程根按源码行断点；调试 ELF 的 `.file` 根相对。
- `tc --version`、三入口 release audit、AGENTS/README/docs map 同步。

## 10. 风险与限制

| 风险 | 处理 |
| ---- | ---- |
| 根发现依赖最近 manifest | 嵌套工程明确允许，最近祖先生效；文档写清 |
| 根外 include 显示 `../` | 外部文件在行表标 `external:true`；不同卷回退 `#<n>` |
| gdb 从非工程根启动找不到源 | 文档要求从根启动或设置 `directory`；T2/T6 验证 |
| AOT `.asciz` 改变产物字节 | 0.0.47 不承诺与 0.0.46 路径字节一致；代码/ABI 不变；跨根一致为验收 |
| 外部 clang/lld 诊断含绝对路径 | 属于外部工具输出，不纳入 TC 路径契约 |
| 测试侧车是二进制 | 仓库 `.gitattributes` 标注 `*.bin -text`；文档提示 |
| `tc` 复用私有 core 造成重复驱动逻辑 | 抽出私有 `run_source` 辅助，`tc` 与 `tc-vm` 共用；不导出 API |
| 侧车/工程目录写入失败 | init 回滚本次新建；test 不写源目录 |
| 嵌套工程/多 manifest 歧义 | 最近祖先规则；不允许"跳过最近 manifest" |
| 删除 legacy 绝对路径模式 | 0.0.47 清洁设计；旧脚本按新路径契约迁移 |

## 11. 里程碑与成本

| 阶段 | 内容 | 依赖 | 成本 |
| ---- | ---- | ---- | ---- |
| **P0** 契约 | 三工具入口、manifest v1、测试侧车、路径契约；release audit/AGENTS/README 设计同步 | — | 2 天 |
| **P1** init/manifest | `tc --init-project`、`tc.project` 解析/根发现、模板、原子写入 | P0 | 4–5 天 |
| **P2** 路径模型 | display 字段、诊断/IR/trap/trace/linemap、external、跨根 | P1 | 1–1.5 周 |
| **P3** tc --test | 发现、侧车、私有 core 驱动、报告、过滤、超时 | P2 | 4–5 天 |
| **P4** AOT/调试 | `.asciz`、`.file`/`.loc`、comp_dir、gdb、跨根产物 | P2 | 1 周 |
| **P5** 验收 | 仓库测试、harness、release audit、文档/门禁 | P3–P4 | 1 周 |

合计约 **4–5 周**（单人，含测试）；与工具链 T1–T3 的路径消费者有重叠，排期以 P0 为前置。

## 12. 0.0.48 移交

- 0.0.47 只做单工程：`entry`、`tests/`、`profile`；多 target/workspace、依赖管理、lockfile
  不在本版本。
- 未来的 `tc --build/--run`、workspace、外部依赖解析以本文件的 manifest v1 为兼容基础。
- RTOS 相关的 board/device 配置不进 `tc.project`；保持工程文件只描述源码、测试与默认档案。

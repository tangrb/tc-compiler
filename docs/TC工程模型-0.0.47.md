# TC 工程模型 0.0.47

## 0. 依据与范围

- 语言语义以 [TC 语言及编译器标准 0.0.46](TC语言及编译器标准-0.0.46.md) 为唯一权威；本文件
  不修改语言标准（51 个 opcode、80 个诊断码、零 pass 基线与 ABI 冻结）。
- 本文件定义 0.0.47 的**工程模型**：工程布局、`tc.project`、根锚点、路径规则、`tc` 工具与
  工程内测试。它是工具链的环境与项目约定，不构成语言可观察行为。
- 0.0.47 的工程模型按清洁设计：**不保留"绝对路径输出"模式**，也不承诺与 0.0.46 的
  工具文本/产物路径字节一致；程序语义、ABI 与零 pass 基线不变。
- 编译器入口仍是 `tc-vm` 与 `tc-aot`；`tc` 是 0.0.47 新增的工程工具，不提供通用编译/运行
  操作；`--test` 只在私有 core 内编译并运行测试入口。
  工程模型与调试/可观测设计的关系见
  [TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md)。

## 1. 工具与契约

| 工具 | 角色 | 编译器入口 |
| ---- | ---- | ---- |
| `tc` | 工程初始化、工程测试、工程元数据 | 否（`--test` 内部使用私有 core） |
| `tc-vm` | 参考 VM，执行最终 IR | 是（前端至最终 IR） |
| `tc-aot` | 原生后端，生成 asm/obj/elf/flat | 是 |

- `tc` 不是编译器入口：不提供用户 entry 的编译/运行操作，不发射目标产物；`--test`
  只为工程测试在私有 core 内编译并运行测试入口。

- 工具清单从两个编译器 CLI 扩展为**两个编译器入口 + 一个工程工具**；
  `tests/tools/s10_release_audit.py`、`src/driver/CMakeLists.txt`、AGENTS 与 README 的入口清单
  必须同步。
- `tc` 与编译器共享退出码：`0` 成功、`1` 语言诊断/trap/测试失败、`2` 用法错误、`70`
  环境不可用、`71` 内部错误；不新增退出码。
- 源读取/宿主 IO 错误映射 70；实现内部错误/文档化实现上限映射 71。`tc` 与 `tc-vm` 共用
  私有 `run_source`，同一失败不得在两个入口映射到不同退出码。
- `tc --version` 打印工具链与语言版本，例如
  `tc 0.0.47 (language 0.0.46; project tool)`。
- `tc` 不提供公共 C 接口、嵌入 API 或构建脚本执行能力。

## 2. 工程布局

工程布局示意（`tc --init-project` 只创建 `src/` 与 `tests/smoke/` 两个目录；`tests/exit/`
只展示侧车布局，不由初始化创建；根文件见 §5）：

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
├── .gitignore
└── .gitattributes           # *.bin 标记为二进制，避免换行转换
```

约定：

- `src/` 是源文件根；`entry` 默认 `src/main.tc`。
- `tests/<case>/main.tc` 是测试入口；测试不与 `src/` 混放，不进入发布构建。
- `build/` 是 out-of-source 生成目录；`tc --test` 不向源目录写生成物。
- 工程内测试发现固定为**一层**：`tests/*/main.tc`；排序后执行，测试名为目录名。
- `profile` 以 `tc.project` 的 `profile` 为准；测试与构建共用该默认值。

## 3. `tc.project`

### 3.1 语法

行式、声明式、无代码执行；`#` 在双引号外起始为注释，只含空格/Tab 的行视为空行：

```text
# tc.project
tc-project 1
name = "demo"
entry = "src/main.tc"
include = "src"
profile = "aarch64-linux-user"
```

- 文件按 UTF-8 读取；允许开头一个 UTF-8 BOM，BOM 不计入第一条非注释行；非法 UTF-8 → 2。
- 第一条非注释行必须是 `tc-project <version>`；版本 token 必须恰为 `1`（不接受 `01`、
  `+1` 或其它写法）。键与值两侧允许 ASCII 空格/Tab；行尾为 LF 或 CRLF，最后一行可省略
  行尾。
- 字符串用双引号；只支持 `\"` 与 `\\` 转义，其它反斜杠转义或尾随反斜杠 → 2；
  不支持多行字符串；引号内 `#` 是值的一部分，不开始注释。
- 空字符串值、未知版本、未知键、重复的单值键（`tc-project` 也视为单值键）、缺 `=`、
  引号外控制字符或格式错误 → 退出 2，不静默忽略；`include` 可重复。
- 工程文件中的路径必须是相对路径；绝对路径 → 退出 2。`name` 允许 `#`/`=` 等普通字符，
  但必须用引号写作字符串。空文件或只有注释/空行的文件按缺少 `tc-project` 退出 2。
- manifest 读取/IO 错误 → 70；解析、字段或路径错误 → 2。编译器与 `tc --test` 使用同一
  映射，不把无效 manifest 静默当作缺省配置。

### 3.2 字段

| 字段 | 必需 | 说明 |
| ---- | ---- | ---- |
| `tc-project` | 是 | 格式版本；当前为 `1` |
| `name` | 否 | 工程名；缺省取工程根目录名。初始化工具写入命令行给的名称 |
| `entry` | 否 | 默认入口，缺省 `src/main.tc`；保留给未来的 `tc --build/--run`；0.0.47 的编译器仍接收显式 source |
| `include` | 否 | 可重复、有序的 import 根；相对工程根；缺省为 `src` |
| `profile` | 否 | 默认档案；缺省 `aarch64-linux-user`；必须是已实现档案；编译器 CLI 的 `--profile` 覆盖它 |

- CLI `--include` 根排在 manifest 的 `include` 之后（manifest 顺序优先，CLI 作为增量）。
  manifest 的 `include` 按工程根解析；CLI `--include` 按调用者 cwd 解析（与命令行 source
  相同），两者 canonical 化后按序参与 import 查找；CLI 根可以是绝对路径，根外显示走
  §4.2 外部规则。
- import 查找遵守标准 §4.5/§4.11.2：先查导入文件所在目录，再按 include 根顺序查找；
  include 根不改变这一优先关系。无 manifest 时 include 根仅来自 CLI（可为零）。
- `include` 允许 `..`；根外文件进入"外部文件"路径规则（§4.2）。
- `entry` 在 0.0.47 只做元数据；`tc-vm`/`tc-aot` 仍以命令行 source 为准。
- `name`（显式或缺省）必须满足 §5 的 `project_name` 约束（非空、合法 UTF-8、拒绝
  控制字符、`/`、`\`、`"`，且名称不得为 `.` 或 `..`）；缺省取工程根目录名，不满足 → 2。
- manifest 中的 `profile` 始终按已实现档案校验；CLI `--profile` 覆盖只选择本次运行的档案，
  不使非法 manifest 变合法。
- `w32` 只有 VM 静态档案；`tc-aot` 使用它按无后端映射退出 70，不回退到其它档案。`tc --test`
  只使用 manifest 的 `profile`，不接受 CLI `--profile`。

## 4. 根锚点与路径

### 4.1 根发现

- 编译器（`tc-vm`/`tc-aot`）：从 **root 模块 canonical 路径**向上找最近的 `tc.project`；
  工程根 = manifest canonical 路径所在目录。找不到 manifest 时，工程根 = root 模块所在目录。
  root 模块不存在时，用其父目录的 canonical 路径（父目录也不存在时，取最近存在祖先的
  canonical 路径并拼接剩余组件）作为根发现起点；该情况仍按源读取错误退出 70。
- `tc --test`：cwd 先 canonical 化，再向上找最近的 `tc.project`；找不到 → 退出 2（工程测试
  必须在工程内）。
- 嵌套工程允许：最近祖先生效；`tc --init-project` 可在已有工程内创建子工程（子工程有
  自己的 `tc.project`）。这是"每个工程一个锚点"，不是"整个文件树一个锚点"。
- manifest、根路径与存在的源路径都先 canonical 化；源文件不存在时按 §4.1 的根发现起点
  与 §4.2 的显示规则处理；显示路径由 canonical 工程根与 canonical 文件路径求相对，
  symlink 的书写形式不进入显示；canonical 目标在根外时按 §4.2 外部规则显示。

### 4.2 唯一锚点

- 所有输出路径都相对**工程根**，不相对 root 模块目录、不相对 include 根、不使用绝对路径。
- 内部 `canonical_path` 用于模块实例键、去重、import 解析与显示路径求相对；显示路径永远
  不是 canonical 绝对路径，**永不直接输出**。
- 显示路径 = 以 canonical 工程根为基准、对 canonical 文件目标求出的相对路径；源文件不
  存在时，用其最近存在祖先的 canonical 路径拼接剩余词法组件后再求相对；symlink 的书写
  形式不进入显示。根内文件形如 `src/main.tc`、`tests/smoke/main.tc`。
- 根外文件（import/include 指到根外）：使用 `../…` 相对路径，并在行表条目标
  `"external":true`；不同卷或不可表示的路径回退 `<basename>#<n>`，编号按本次编译已知源
  文件的 canonical 路径字节序确定；回退项同样标 `"external":true`。部分检查模式同样适用，
  不依赖最终 IR 是否生成。

### 4.3 路径消费者

以下输出一律使用显示路径，覆盖所有编译模式（含 `--syntax-only`/`--sem-only`/`--static-only`）：

- 文本诊断、相关位置、LEX 非法字节 note；诊断 JSON。
- 实现错误、源读取/IO 错误与路径解析 note 中的源路径。
- VM trap 文本与 JSON trap 行。
- `--dump-ir` 的 `modules:` 段。
- trace 的 `span` 与 header `source`；linemap 的 `source`/`file`。
- 调试 ELF 的 `.file` 与符号所在源文件；AOT `.file`/`.loc`/`# source:`。
- AOT 产物内的源文件字符串（`.asciz`）与目标运行时 trap 记录。
- `monitor frames`/`ir`/`funcs` 的源码位置。
- `tc --test` 的失败诊断、源位置、侧车读取/格式错误与 `tests` 布局错误中的路径；报告中的
  `<test>` 是逻辑测试路径（§6.4）。
- `tc --init-project` 的输出与错误消息中的工程路径。

无 `--trace-paths=abs`、`--debug-path-map` 一类绝对路径开关；0.0.47 不提供绝对输出模式。
外部工具（clang/lld）自身打印的绝对临时路径不属于 TC 输出契约，属于外部工具诊断。
`tc --test` worker 的 fork、管道与临时捕获文件只服务进程隔离；其宿主路径、PID 与实际
耗时不属于 TC 输出，报告只使用测试名和工程相对路径。

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
tc --init-project [--] project_name
```

- 当前目录就是工程根；不创建 `project_name/` 子目录；`project_name` 只写入 `name`。
- `project_name`：非空、合法 UTF-8；拒绝 `/`、`\`、控制字符（Unicode Cc）与 `"`；
  名称为 `.` 或 `..` 时拒绝，含其它 `.` 的名称允许；允许空格与普通 UTF-8。
- 当前目录已有 `tc.project` → 退出 2。
- 当前目录非空 → 允许；只创建缺失文件，不覆盖已有 `src/main.tc`、`tests/...`、`.gitignore`、
  `.gitattributes`；目标路径的最终组件以不跟随 symlink 的存在性判断，已有（含 symlink）即
  打印 `skipped`。目标路径的已存在前缀目录组件（如 `src`、`tests`）若是 symlink 或不是
  普通目录，不跟随、不写穿，该目标同样按 `skipped` 处理。
- 生成：
  - `tc.project`：`tc-project 1`、`name`、`entry = "src/main.tc"`、`include = "src"`、
    `profile = "aarch64-linux-user"`；
  - `src/main.tc`：最小合法 `#program` 模板（`_ = writeln int32, 1`）；
  - `tests/smoke/main.tc` 与 `tests/smoke/stdout.bin`：与模板一致的 smoke 测试
    （stdout 字节为 `31 0a`，即 `1\n`）；
  - `.gitignore`：`build/`；
  - `.gitattributes`：`*.bin -text`（测试侧车是精确字节）。
- 不创建 `build/`，不调用编译器，不写任何绝对路径。
- 输出：先打印 `initialized project "<name>"`，再按固定目标顺序打印 `created`/`skipped` 的
  **相对路径**，最后打印 `next: tc --test`；不含 cwd 绝对路径。
- 退出码：0 成功（含 skipped）；2 已有工程/名称非法；70 IO/环境；71 内部。
- 写入失败：删除本次新建的文件及本次新建的空目录，保留原有文件/目录，退出 70。
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
created .gitattributes
next: tc --test
```

## 6. `tc --test`

### 6.1 发现

- 仅发现 `tests/*/main.tc`（一层）；`tests/` 解析后必须是目录，否则退出 2。包含普通文件
  `main.tc` 的子目录是测试目录；其目录名必须是合法 UTF-8 且不含控制字符（Unicode Cc），
  否则退出 2（避免不可解析的报告）。其它子目录忽略；按目录名的 UTF-8 字节序（逐字节
  无符号比较）排序。
- 测试名 = 目录名，按字节精确匹配。
  无参数执行全部，带参数筛选（重复名称只执行一次），筛选后仍按同一顺序执行；所有筛选
  参数先校验，任一未知名称 → 退出 2 且不执行任何测试。
- `tests/` 不存在：无筛选参数时 0 个测试，退出 0 并打印 `0 tests`；带筛选参数时所有名称
  均为未知，退出 2。目录枚举/读取失败退出 70；测试源读取失败退出 70，不当作测试失败。

### 6.2 侧车文件

| 文件 | 含义 | 缺省 |
| ---- | ---- | ---- |
| `stdout.bin` | 期望 stdout 精确字节 | 空 |
| `stdin.bin` | 喂给程序的 stdin 字节 | 空 |
| `trap` | 期望首个 `TC_RE_*`（UTF-8/ASCII 文本；去掉至多一个 LF 或 CRLF 行尾后精确匹配） | 无 trap |
| `exit` | 期望退出码（ASCII 无符号十进制，0..255；可带一个 LF/CRLF 行尾） | 有 `trap` 时为档案 trap 状态（当前 1），否则 0 |

- `stdout.bin`/`stdin.bin` 是二进制精确字节；不使用文本换行转换；侧车路径必须是普通文件，
  读取/存在性错误退出 70。
- `trap`/`exit` 只去掉至多一个行尾 LF 或 CRLF；其它空白保留，不做 trim 或数值归一化。
- `trap` 必须是合法 UTF-8 的单行文本、以 `TC_RE_` 开头；去掉至多一个行尾后不得为空，
  不得再含 LF/CR；非法 UTF-8 按格式错误退出 2。
- `exit` 必须是 ASCII 无符号十进制 0..255；不接受符号、前后空格、多于一个行尾或其它字符。
- `trap`/`exit` 格式错误（空 `trap`、非 `TC_RE_`、非法 `exit`、额外行尾或行内换行）→
  退出 2；该测试不执行，此前已执行测试的报告保留；这不是测试失败，也不静默忽略。
- `trap` 缺省表示期望正常终止；`exit` 显式给出时必须与运行结果一致。VM 正常终止为 0，
  trap 为档案 trap 状态（当前 1）；其它值必然失败，不作为用法错误。
- `trap`/`exit` 的实际值取自 worker 的运行结果（首个 trap 码与退出状态），不扫描
  stdout/stderr；程序输出中的 `TC_RE_*` 文本不参与判定。
- 测试目录本身是 include 根（优先于 manifest 的 `include`，import 仍先查导入者目录），
  再叠加 manifest 的 include 顺序。
- 测试入口必须能通过静态阶段；编译诊断按普通诊断格式输出，测试计为失败。

### 6.3 执行

- 每个测试由一个私有 worker 执行：父进程 fork 后，worker 复用私有 core 的加载与
  `tc_vm_run`，不 exec `tc-vm`，不调用 AOT/clang/lld，不导出公共 API。无论是否设置
  超时都使用 worker；测试按名称顺序串行执行。
- 父进程确定工程根、profile 与 include 顺序后传给 worker；worker 不重新从测试源路径
  做根发现。worker 的 cwd = 工程根；stdin 始终来自 `stdin.bin`（缺省为空，立即 EOF），
  不继承调用者终端；stdout/stderr 由父进程捕获。stdout 与 `stdout.bin` 比较；stderr 不
  参与通过判定，仅作为失败详情输出。每个测试使用全新的 frontend/IR/CT/VM 状态。
- 父进程在 fork 前读取并校验当前测试的侧车；读取/存在性错误退出 70，格式错误退出 2；
  两者均停止后续测试并保留已输出报告。deadline 不覆盖父进程的侧车读取与结果比较。
- 默认不设 deadline；`tc --test --test-timeout=<seconds>` 可选设置单测试 deadline。
  `<seconds>` 只含 ASCII 十进制数字、非负、不接收符号/空格/小数；缺省或 `0` 表示不限，
  正数从 worker 开始执行时计时，覆盖加载、CT/IR 与 VM 执行。父进程用单调时钟计时；
  到期后向 worker 发送 SIGKILL 并回收，测试计为失败（退出 1），报告稳定的 timeout 文本
  并继续后续测试。超时前捕获的 stdout 只用于失败详情，不作为通过依据。
- `stdin.bin` 以只读文件描述符直接作为 worker stdin（或等价地并发写入），不因大输入
  阻塞父进程的 stdout/stderr 排空。
- 实际输出与期望逐字节比较；失败时打印测试名、stdout 差异、stderr、trap/exit 差异；
  TC 生成的路径字段 root 相对，不含绝对路径；测试程序输出本身按原字节保留，不做路径改写。
- 不生成 build 产物；不向 `tests/` 写文件。worker 的管道/临时捕获文件位于工程外，
  其宿主路径不进入报告或诊断。

### 6.4 输出与退出码

```text
project "demo" (aarch64-linux-user)
ok   tests/smoke
FAIL tests/exit: trap expected TC_RE_DIV_ZERO, got none
1 passed, 1 failed
```

- 报告中的 `<test>` 是逻辑测试路径 `tests/<目录名>`；筛选、排序与报告用它，源位置/失败
  诊断仍用 §4.3 的 display 路径。
- 成功：0；有失败：1；用法/无工程/未知测试名/`tests` 布局或侧车格式错误：2；环境或 IO：
  70；内部：71。stdout/trap/exit/编译诊断/timeout 失败计 1 并继续后续测试；2/70/71 停止。
- timeout 报告为 `FAIL <test>: timeout after <seconds>s`；`<seconds>` 是参数原文，不打印
  实际耗时或 PID。timeout 计 failed，后续测试继续。
- fork 失败或 worker 返回源读取/环境 IO 错误：退出 70；worker 路径停止后续测试并保留
  已输出报告。worker 报告语言/静态诊断时按普通诊断格式输出，测试计为失败（退出 1）；
  非超时的 worker 异常终止、内部错误或结果协议损坏：退出 71，停止后续测试并保留已输出
  的报告。
- 报告与汇总写 stdout；编译诊断与失败详情写 stderr；TC 生成的路径字段没有绝对路径、
  颜色或时间戳。测试程序 stdout 按原字节与 `stdout.bin` 比较；stderr 按原字节保留，只在
  失败详情中输出；0.0.47 没有 stderr 侧车，二者都不做路径改写。成功测试的 stderr 不输出。
  父进程写出的 stdout 与 stderr 行各自按测试名排序，跨流相对顺序不构成契约。

## 7. `tc` CLI

```sh
tc --init-project [--] project_name
tc --test [--test-timeout=<seconds>] [--] [test_name...]
tc --help
tc --version
```

- `tc` 不编译、不运行用户 entry；`--test` 内部编译/运行的是测试入口；用户程序仍由
  `tc-vm`/`tc-aot` 编译。`--test-timeout` 只属于 `--test`，与 `--init-project` 同时出现
  或单独出现 → 2；未给出时不设单测试超时；只接受 `--test-timeout=<seconds>` 形式。
- `--init-project` 与 `--test` 不能同时出现；`--help`/`--version` 必须单独使用，与任何
  工程操作或其它参数同时出现 → 2；未知参数/多余参数/没有操作 → 2。
- `--init-project`、`--test`、`--help`、`--version`、`--test-timeout` 不得重复；违反 → 2。
- `--` 结束选项解析，其后参数即使以 `-` 开头也按位置参数处理；选项解析中的 `--` 至多
  一次，只用于 `--init-project` 或 `--test` 的参数中；违反 → 2。`--` 之后的 `--` 是
  位置参数，不受此限。`--test` 后选项与测试名可任意顺序。
- 未来 `--build`/`--run` 若加入，复用 `entry`/`profile`；0.0.47 不实现。

## 8. 实现落点

| 模块 | 内容 |
| ---- | ---- |
| `src/driver/tc.c` | `tc` 入口、`--init-project`/`--test`/`--help`/`--version`、`--test-timeout` 解析、退出码 |
| `src/project/manifest.[ch]` | `tc.project` 解析、根发现、字段校验、默认值 |
| `src/project/init.[ch]` | 模板与原子写入、名称校验、skipped 报告 |
| `src/project/test_run.[ch]` | 测试发现、侧车读取与格式校验、父进程调度/捕获/超时/回收、比较与报告 |
| `src/project/test_worker.[ch]` | fork 后的 worker 入口、fd/stdio 捕获、结果协议、`_exit` 与 FD 清理 |
| `src/base/path.[ch]` | root/canonical/display、外部 `../`、`#<n>`、路径一致性 |
| 前端 | `TcSpan` 增加 display；`TcModuleInfo`/`TcParseModule`/`TcModule` 增加 `display_path`；lex/parse/module/sem 诊断统一用 display |
| 编译器驱动 | `tc-vm`/`tc-aot` 读 manifest、确定根（含部分检查模式）、`--include` 顺序、错误码；诊断/impl/IO 源路径统一 display |
| 后端 | 三后端统一 `tc_span_display()`；`.asciz`、`.file`/`.loc`、`# source:` |
| VM/调试 | trap、trace、linemap、debug ELF、`monitor` 用 display |
| 构建 | `CMakeLists.txt` 新 `tc` target、project/path 模块；`tc`/`tc-vm` 共享私有 core；release audit 三入口 |

- `tc` 链接私有 `tc_core`；不新增公共 C 库、不安装头文件。`tc` 与 `tc-vm` 共享私有
  `run_source`（加载、诊断、CT/IR、`tc_vm_run`），worker 只负责隔离、捕获与结果协议。
- 父进程在 fork 前完成 manifest/路径解析，把根、profile 与 include 顺序交给 worker；
  worker 不再做根发现。父进程负责 deadline、SIGKILL 与 `waitpid` 回收；worker 不直接写
  用户报告。
- 捕获管道必须由父进程并发排空或落到工程外临时文件，避免大输出阻塞；所有退出路径统一
  回收 worker、关闭 fd 并清理临时文件。
- 结果协议只记录结构化测试结果（诊断、impl/IO 错误类别、trap、退出码、stdout/stderr
  捕获），不承载 PID 或实际耗时；协议为私有定义，写入 `src/CONTRACTS.md`，不导出公共 API。
- worker 隔离使用 POSIX fork/pipe；这是 0.0.47 的宿主约束，不改变语言语义与 ABI。
- `tc.project` 解析器不依赖外部 TOML/JSON 库；错误信息包含行号，路径 root 相对。
- `--syntax-only` 不跟随 import，但仍必须确定工程根并使用 display；不得直接输出命令行
  source 的绝对路径；`--sem-only`/`--static-only` 同受此约束。
- 所有路径输出共享同一 `tc_path_display`，禁止各后端/工具自行拼路径；impl/IO 错误 detail
  也必须先经 display 或去掉宿主绝对路径。

## 9. 验收与测试

### 9.1 仓库测试位置

| 内容 | 位置 | 标签 |
| ---- | ---- | ---- |
| `tc` CLI：init/version/help/退出码、`--test-timeout` 取值、`--` 与组合参数 | `tests/project/tc_cli_test.py` | `toolchain`, `fast` |
| manifest：语法、引号内 `#`、空值、CRLF、默认值、根发现、非法项 | `tests/project/manifest_test.py` | `toolchain`, `fast` |
| 工程测试 runner：发现、侧车格式、过滤预校验、失败、worker 隔离、timeout、stdin EOF/大输入、stderr 判定、大输出 | `tests/project/test_runner_test.py` | `toolchain`, `fast` |
| 路径：诊断/trap/IR/trace/linemap/AOT 字符串、部分检查模式、impl/IO 错误 | `tests/project/path_test.py` | `toolchain` |
| 跨根可复现 asm/ELF/flat | `tests/determinism/project_determinism.py` | `toolchain`, `slow` |
| 三入口/release audit | `tests/tools/s10_release_audit.py`、`tests/tools/cli_contract_test.py` | `toolchain` |
| harness adapter 的 root 解析 | `tests/adapters/`、`tests/harness/` | `conformance` |

### 9.2 验收

- `tc --init-project` 在临时目录生成完整工程；生成文件无绝对路径；`.gitattributes` 覆盖
  `*.bin`；smoke 通过 `tc --test`。
- 已有 `tc.project`、非法名称、非空目录、已有文件、最终/中间 symlink、权限失败均有确定
  行为；`src`/`tests` 为 symlink 时不写穿，外部目标保持不变；`tc --init-project -- -demo`
  可初始化以 `-` 开头的合法名称；含其它 `.` 的名称（非 `.`/`..`）可初始化。
- manifest 的版本/未知键/重复键/空值/绝对路径/非法转义/非法 profile/非法缺省名称全部
  退出 2；读取/IO 错误退出 70；include 顺序与基准确定（manifest 根相对、CLI cwd 相对），
  import 仍先查导入者目录；引号内 `#`、引号外注释与 LF/CRLF 行尾按 §3.1 解析；版本只有
  `1` 合法（`01`/`+1` 为 2），最后一行可无行尾；空文件/仅注释为 2。
- manifest `profile = "w32"` 在 `tc-vm`/`tc --test` 可用；在 `tc-aot` 退出 70 且不生成产物。
- 从工程根、子目录、工程外调用 `tc-vm`/`tc-aot`，诊断/trap/IR/trace/linemap 路径一致且
  根相对；无 `/Users/`、`/home/`、`/private/` 等绝对路径；`--syntax-only`/`--sem-only`/
  `--static-only`、缺失源文件与 impl/IO 错误同样不泄露绝对源路径；侧车/`tests` 布局错误
  路径也 root 相对；源读取/宿主 IO 为 70，实现内部/上限为 71，且 `tc`/`tc-vm` 一致。
- 两个不同根目录构建同一工程，asm/ELF/flat 逐字节一致；代码/ABI 语义不变。
- `tc --test`：stdout/stdin 精确字节、trap/exit 侧车、失败报告、过滤、无测试；测试目录
  作为 include 根先于 manifest 的 include，worker 不继承终端 stdin；stderr 不参与判定、
  成功不输出、失败时进入失败详情；显式
  `--test-timeout` 对不终止测试返回 1、报告 timeout 文本且后续测试继续；缺省/`0` 不限；
  负数、带符号/空格/小数、非整数、溢出、缺值、单独出现或与 `--init-project` 同现退出 2；
  结束后无残留 worker/临时文件。
- `tc --test` 的侧车格式错误（空/非 `TC_RE_`/非法 UTF-8 的 `trap`、非法 `exit`、额外
  行尾）退出 2 并停止后续测试，出错测试不执行；`tests` 为普通文件、测试名含 Cc 控制字符
  或非法 UTF-8 退出 2；未知筛选名在 fork 前整体拒绝；`--` 后以 `-` 开头的目录名可按测试名
  执行；程序输出中的 `TC_RE_*` 文本不被当作 trap 结果。
- `tc --test` 的大 stdout/大 stdin 不因管道阻塞；fork 失败退出 70；worker 非超时异常终止
  或结果协议损坏退出 71，且不产生伪通过。
- gdb 可用时从工程根按源码行断点；调试 ELF 的 `.file` 根相对；gdb 缺失按工具链
  §7.1/§8 显式跳过并记录原因。
- `tc --version`、三入口 release audit、AGENTS/README/docs map 同步。

## 10. 风险与限制

| 风险 | 处理 |
| ---- | ---- |
| 根发现依赖最近 manifest | 嵌套工程明确允许，最近祖先生效；文档写清 |
| 缺失源文件无法 canonical 化 | 用父目录/最近存在祖先的 canonical 路径作为根发现与显示起点；源读取仍 70，不泄露绝对路径 |
| 已有 `.gitattributes`/`.gitignore` 缺少规则 | init 不修改已有文件；工程需自行保留 `*.bin -text` 与 `build/` 忽略；新建文件场景由验收覆盖 |
| 根外 include 显示 `../` | 外部文件在行表标 `external:true`；不同卷回退 `#<n>` |
| manifest 的注释/空值/换行歧义 | §3.1 固定引号内 `#`、空值非法、LF/CRLF；测试覆盖 |
| `profile` 与工具不兼容 | `w32` 仅 VM；`tc-aot` 退出 70，不回退 |
| 侧车格式错误被误报为测试失败 | 父进程在 fork 前校验；格式错误退出 2，停止后续测试 |
| `tests` 布局异常或测试名不可打印 | `tests` 非目录、测试名含 ASCII 控制字符或非法 UTF-8 退出 2；不生成歧义报告 |
| gdb 从非工程根启动找不到源 | 文档要求从根启动或设置 `directory`；T2/T6 验证 |
| AOT `.asciz` 改变产物字节 | 0.0.47 不承诺与 0.0.46 路径字节一致；代码/ABI 不变；跨根一致为验收 |
| 外部 clang/lld 诊断含绝对路径 | 属于外部工具输出，不纳入 TC 路径契约 |
| 测试侧车是二进制 | init 在缺少 `.gitattributes` 时生成 `*.bin -text`；已有文件不修改；0.0.47 落地时仓库 `.gitattributes` 同步添加该规则并纳入验收 |
| `tc` 复用私有 core 造成重复驱动逻辑 | 抽出私有 `run_source` 辅助，`tc` 与 `tc-vm` 共用；不导出 API |
| 侧车/工程目录写入失败 | init 回滚本次新建；test 不写源目录 |
| init 目标前缀是 symlink | 不跟随、不写穿；按 skipped 处理；验收检查外部目标不变 |
| 测试程序不终止 | 默认无 deadline；所有测试在 worker 中执行；`--test-timeout` 到期由父进程发送 SIGKILL 并回收，计为失败；无超时由 CI 作业兜底；不把固定时长写成契约 |
| 管道/大输入/大输出阻塞或子进程泄漏 | stdin 用文件描述符或等价并发提供；父进程并发排空捕获或使用工程外临时文件；所有路径统一 `waitpid`、关闭 fd、清理；大输入/大输出回归 |
| 超时测试自身不稳定 | 验收只断言 timeout 失败与后续继续，不断言精确耗时；测试使用宽松秒数 |
| POSIX fork/pipe 依赖 | 0.0.47 `tc --test` worker 只面向 POSIX 宿主；其它平台另案 |
| 非超时 worker 异常终止/协议损坏 | 退出 71，停止套件并保留已输出报告；不把子进程状态当成测试通过 |
| 嵌套工程/多 manifest 歧义 | 最近祖先规则；不允许"跳过最近 manifest" |
| 删除 legacy 绝对路径模式 | 0.0.47 清洁设计；旧脚本按新路径契约迁移 |

## 11. 里程碑

| 阶段 | 内容 | 依赖 |
| ---- | ---- | ---- |
| **P0** 契约 | 三工具入口、manifest v1、测试侧车、worker/超时契约、路径契约；release audit/AGENTS/README 设计同步 | — |
| **P1** init/manifest | `tc --init-project`、`tc.project` 解析/根发现、模板、原子写入 | P0 |
| **P2** 路径模型 | display 字段、诊断/IR/trap/trace/linemap、external、跨根 | P1 |
| **P3** tc --test | 发现、侧车、worker 隔离/超时/捕获、私有 core 驱动、报告、过滤 | P2 |
| **P4** AOT/调试 | `.asciz`、`.file`/`.loc`、comp_dir、gdb、跨根产物 | P2 |
| **P5** 验收 | 仓库测试、harness、release audit、文档/门禁 | P3–P4 |

与工具链 T1–T3 的路径消费者有重叠；P3 的 worker/超时与工具链 T5 测试基建对齐；P0 是前置。

## 12. 0.0.48 移交

- 0.0.47 只做单工程：`entry`、`tests/`、`profile`；多 target/workspace、依赖管理、lockfile
  不在本版本。
- 未来的 `tc --build/--run`、workspace、外部依赖解析以本文件的 manifest v1 为兼容基础。
- RTOS 相关的 board/device 配置不进 `tc.project`；保持工程文件只描述源码、测试与默认档案。

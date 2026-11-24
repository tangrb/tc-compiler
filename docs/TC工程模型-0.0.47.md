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
- 源读取/宿主 IO 与内存耗尽映射 70；实现内部错误/文档化实现上限映射 71。`tc` 与
  `tc-vm` 共用私有 `run_source`，同一失败不得在两个入口映射到不同退出码。根模块首个有效
  模式为 `#lib` 时，`tc-vm`/`tc-aot`/`tc --test` 统一按实现/API 用法错误退出 2，不产生
  语言诊断、不执行或发射产物；该判定先于模式行后的 LEX/SYN 错误。
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
- `tc.project` 解析后必须是普通文件；目录、FIFO、设备或 dangling symlink → 70，不打开
  可能阻塞的非普通文件。
- `tc.project` 单文件大小上限 4 MiB；超过按文档化实现上限退出 71，不整体读入内存。
- 检查顺序固定为：`open`（以非阻塞或等价方式打开，避免 FIFO 等阻塞）→ `fstat`
  （普通文件与大小）→ 超过 4 MiB 退出 71 → 最多读取 4 MiB+1 并复核 EOF → UTF-8/字段/路径解析退出 2。
  读取期间增长或累计读取超过 4 MiB 同样退出 71，不解析已读内容。
  `open`/`fstat`/读取的宿主 IO 失败退出 70。因此“超过
  4 MiB + 非法 UTF-8”是 71，“open 失败（不可读）+ 超过 4 MiB”是 70，“超限 + 未知键”
  是 71；`fstat` 成功且超限时不再读取，读取阶段失败仍是 70。
- manifest 读取/IO 错误 → 70；解析、字段或路径错误 → 2；超过 4 MiB → 71。编译器与
  `tc --test` 使用同一映射，不把无效 manifest 静默当作缺省配置。

### 3.2 字段

| 字段 | 必需 | 说明 |
| ---- | ---- | ---- |
| `tc-project` | 是 | 格式版本；当前为 `1` |
| `name` | 否 | 工程名；缺省取工程根目录名。初始化工具写入命令行给的名称 |
| `entry` | 否 | 默认入口，缺省 `src/main.tc`；保留给未来的 `tc --build/--run`；0.0.47 的编译器仍接收显式 source |
| `include` | 否 | 可重复、有序的 import 根；相对工程根；缺省为 `src` |
| `profile` | 否 | 默认档案；缺省 `aarch64-linux-user`；必须是已实现档案；编译器 CLI 的 `--profile` 覆盖它 |

- CLI `--include` 根排在 manifest 的 `include` 之后（manifest 顺序优先，CLI 作为增量）。
  manifest `include` 与 CLI `--include` 合计至多 32 个；超出按字段/用法错误退出 2。
  manifest 的 `include` 按工程根解析；CLI `--include` 按调用者 cwd 解析（与命令行 source
  相同），两者 canonical 化后按序参与 import 查找；CLI 根可以是绝对路径，根外显示走
  §4.2 外部规则。
- import 查找遵守标准 §4.5/§4.11.2：先查导入文件所在目录，再按 include 根顺序查找；
  include 根不改变这一优先关系。无 manifest 时 include 根仅来自 CLI（可为零）。
- `include` 允许 `..`；根外文件进入"外部文件"路径规则（§4.2）。
- 不存在的 include 根是合法的空候选根：只能用最近存在祖先的 canonical 路径拼接剩余词法
  组件做显示；搜索时跳过（与无匹配候选相同），不报路径错误；全部根仍无匹配才报
  `TC_CE_IMPORT_NOT_FOUND`。缺省 `include="src"` 且 `src` 不存在同此规则；只有路径字符/
  语法非法才按来源退出 2。
- include 根存在但 canonical 化后不是目录（普通文件、FIFO、设备、socket，或 symlink
  指向非目录）是字段/路径错误，退出 2，不打开目标；canonical 化因目标不存在
  （dangling）或 symlink 环（ELOOP）失败同样按路径错误退出 2；路径中已有非目录前缀
  （`ENOTDIR`，如 `src/main.tc/x` 而 `src/main.tc` 是普通文件）也按路径错误退出 2；
  `EACCES`、`EIO` 等宿主访问错误退出 70。该规则对 manifest `include`、CLI `--include` 与缺省 `include`
  一致；只有词法目标“不存在”仍按空候选跳过。
- `entry` 在 0.0.47 只做元数据；`tc-vm`/`tc-aot` 仍以命令行 source 为准。
- `name`（显式或缺省）必须满足 §5 的 `project_name` 约束（非空、合法 UTF-8、拒绝
  Unicode Cc/Cf/Zl/Zp、`/`、`\`、`"`，且名称不得为 `.` 或 `..`）；缺省取工程根目录名，
  不满足 → 2。
- manifest 中的 `profile` 始终按已实现档案校验；CLI `--profile` 覆盖只选择本次运行的档案，
  不使非法 manifest 变合法。
- `w32` 只有 VM 静态档案；`tc-aot` 使用它按无后端映射退出 70，不回退到其它档案。`tc --test`
  只使用 manifest 的 `profile`，不接受 CLI `--profile`。

## 4. 根锚点与路径

### 4.1 根发现

- 编译器（`tc-vm`/`tc-aot`）：从 **root 模块 canonical 路径所在目录**开始向上逐级搜索
  `tc.project`。搜索以目录项存在为准：`lstat` 返回 ENOENT/ENOTDIR 时继续向上；返回其它
  错误（如 EACCES/ELOOP/EIO）→ 退出 70，不跳过。某个目录存在名为 `tc.project` 的条目
  （普通文件、symlink 或其它类型）时，它就是最近锚点；读取/IO 失败按 §3.1 退出 70、
  解析/字段/路径错误 2、超过 4 MiB 71，不得跳过它继续向外层工程回退。工程根 = 包含该
  目录项的目录的 canonical 路径；`tc.project` 是 symlink 时只跟随其内容，不把工程根改到
  链接目标所在目录。`tc.project` 解析后必须是普通文件（拒绝目录、FIFO、设备等），否则
  退出 70。
- 找不到 manifest 时，工程根 = root 模块所在目录。root 模块不存在时，用其父目录的
  canonical 路径（父目录也不存在时，取最近存在祖先的 canonical 路径并拼接剩余组件）作为
  根发现起点；该情况仍按源读取错误退出 70。此时允许读取 manifest 以确定根相对显示路径，
  但 manifest 的任何错误都不得取代源缺失的 70。root 模块 canonical 化因 ELOOP、ENOTDIR、EACCES、
  EIO 等失败时，先按源路径/宿主 IO 错误退出 70，不发现或解析 manifest；不存在（ENOENT）
  仍按本句的缺失源规则处理。root 模块存在但解析后不是普通文件（目录、FIFO、设备、
  socket 等）同样先退出 70，不发现或解析 manifest；因此 root 的 ENOENT/非普通文件错误
  与损坏 manifest 双错时固定为 70。已存在的 root 普通文件与损坏 manifest 双错时则先报
  manifest 错误，因为 root 内容尚未打开。
- `tc --test`：cwd 先 canonical 化，再按同一规则向上找最近的 `tc.project`；找不到 → 退出 2
  （工程测试必须在工程内）；最近的 `tc.project` 损坏时按 70/2/71 退出，不回退到外层工程。
- 嵌套工程允许：最近祖先生效；`tc --init-project` 可在已有工程内创建子工程（子工程有
  自己的 `tc.project`）。这是"每个工程一个锚点"，不是"整个文件树一个锚点"。
- manifest、根路径与存在的源路径都先 canonical 化；源文件不存在时按 §4.1 的根发现起点
  与 §4.2 的显示规则处理；显示路径由 canonical 工程根与 canonical 文件路径求相对，
  symlink 的书写形式不进入显示；canonical 目标在根外时按 §4.2 外部规则显示。

### 4.2 唯一锚点

- 所有编码进诊断、报告、trace/IR/行表与目标产物的源文件、模块和工程路径都相对
  **工程根**，不相对 root 模块目录、不相对 include 根、不使用绝对路径。该规则约束
  TC 生成内容，不禁止 CLI 输出文件参数使用绝对宿主路径；`/dev/stdout`、`/dev/fd/1`
  等描述符别名按工具链 §5.1 的身份规则处理。
- 内部 `canonical_path` 用于模块实例键、去重、import 解析与显示路径求相对；显示路径永远
  不是 canonical 绝对路径，**永不直接输出**。
- 显示路径 = 以 canonical 工程根为基准、对 canonical 文件目标求出的相对路径；源文件不
  存在时，用其最近存在祖先的 canonical 路径拼接剩余词法组件后再求相对；symlink 的书写
  形式不进入显示。根内文件形如 `src/main.tc`、`tests/smoke/main.tc`。
- 根外文件（import/include 指到根外）：使用 `../…` 相对路径，并在行表条目标
  `"external":true`；不同卷或不可表示的路径回退 `./<basename>#<n>`。回退编号只对需要回退的
  文件分配：文件首次进入显示路径时从 1 递增，同一编译内永不复用或重排；`./` 是回退命名空间
  保留前缀，正常显示路径由 canonical 相对路径归一化后不以 `./` 开头；编号一经分配即
  固定，后续诊断、linemap、trace 不会改变它。`<basename>` 是 canonical 路径的最后一个
  组件；不同编译模式因已发现文件集不同，编号可以不同，但书写规则一致。回退项同样标
  `"external":true`。部分检查模式同样适用，不依赖最终 IR 是否生成。
- 显示路径中出现的源路径与 include 根组件必须是合法 UTF-8 且不含 Unicode Cc/Cf/Zl/Zp。
  失败时按来源退出：CLI 参数或 manifest 值 → 2；缺省工程名与测试目录名按 §5/§6.1 的
  名称规则 → 2；仅由文件系统枚举发现的其它路径 → 70。文本诊断、JSON、`# source:` 与
  汇编字符串依赖该约束；引号与反斜杠仍按各格式规则转义。
- import 名是 ASCII 标识符（标准 §2.3、§4.11.2），其路径组件只有 `<ident>.tc`；本规则
  的“用户提供路径”只覆盖 CLI 参数与 manifest 值，include 根与导入者目录链按各自的来源
  （CLI/manifest 或文件系统发现）处理。

### 4.3 路径消费者

以下输出一律使用显示路径，覆盖所有编译模式（含 `--syntax-only`/`--sem-only`/`--static-only`）：

- 文本诊断、相关位置、LEX 非法字节 note；诊断 JSON。
- 实现错误、源读取/IO 错误与路径解析 note 中的源路径。
- VM trap 文本与 JSON trap 行。
- `--dump-ir` 的 `modules:` 段。
- trace 的 `span`、header `source` 与 `module`/`callee_module`；linemap 的 `source`/`file`/`module`。
- trap 文本/JSON 的 `module`；`monitor funcs`/`frames`/`trap`/`vreg` 的 `module` 字段或选择器。
- 调试 ELF 的 `.file` 与符号所在源文件；AOT `.file`/`.loc`/`# source:`。
- AOT 产物内的源文件字符串（`.asciz`）与目标运行时 trap 记录。
- `monitor frames`/`ir`/`funcs` 的源码位置。
- `tc --test` 的失败诊断、源位置、侧车读取/格式错误与 `tests` 布局错误中的路径；报告中的
  `<test>` 是逻辑测试路径（§6.4）。
- `tc --init-project` 的输出与错误消息中的工程路径。

- root 模块必须是普通文件；目录、FIFO、设备、socket 或 dangling symlink → 退出 70，
  不以可能阻塞的方式打开。import 搜索按标准 §4.5/§4.11.2 取首个解析成功的目标；
  该目标存在但不是普通文件（目录、FIFO、设备、socket 等）时按源读取/宿主 API 错误退出
  70，不跳过它改选后续 include 根。仅目标不存在时继续搜索；全部根无匹配才报告
  `TC_CE_IMPORT_NOT_FOUND`。最终普通文件执行 `open`+`fstat` 复核。单源文件读取上限
  4 MiB；`fstat` 超限直接退出 71，否则最多读取 4 MiB+1 并复核 EOF，读取期间增长或超限
  同样退出 71。
- 路径检查与打开必须基于同一描述符或等价原子操作（`open`+`fstat`；init 使用
  `O_CREAT`/`O_EXCL`/`O_NOFOLLOW`），不得把 `lstat`-then-`open` 作为唯一防线。

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
- `project_name`：非空、合法 UTF-8；拒绝 `/`、`\`、Unicode Cc/Cf/Zl/Zp 与 `"`；
  名称为 `.` 或 `..` 时拒绝，含其它 `.` 的名称允许；允许空格与普通 UTF-8。
- 名称校验后、任何目录创建或文件写入前，先 `lstat` 当前目录的 `tc.project`：条目存在
  （含 symlink）→ 退出 2 且不写入；ENOENT/ENOTDIR → 继续；其它错误 → 70。该检查优先于
  `tests` 预检，已有工程不进入 skipped 规则。
- 写入前预检 `tests`：`lstat` 返回 ENOENT/ENOTDIR → 视为缺失并由 init 创建；其它错误
  → 70；条目存在时必须是真实目录（非 symlink），否则 → 2，不写入任何目标。
- 当前目录非空 → 允许；只创建缺失文件，不覆盖已有 `src/main.tc`、`tests/...`、`.gitignore`、
  `.gitattributes`；目标路径的最终组件以不跟随 symlink 的存在性判断，已有（含 symlink）即
  打印 `skipped`。目标路径的已存在前缀目录组件（如 `src`）若是 symlink 或不是普通目录，
  不跟随、不写穿，该目标同样按 `skipped` 处理；`tests` 由上述预检处理。
- 生成：
  - `tc.project`：`tc-project 1`、`name`、`entry = "src/main.tc"`、`include = "src"`、
    `profile = "aarch64-linux-user"`；
  - `src/main.tc`：最小合法 `#program` 模板（`_ = writeln int32, 1`）；
  - `tests/smoke/main.tc` 与 `tests/smoke/stdout.bin`：与模板一致的 smoke 测试
    （stdout 字节为 `31 0a`，即 `1\n`）；
  - `.gitignore`：`build/`；
  - `.gitattributes`：`*.bin -text`（测试侧车是精确字节）。
- 规范操作顺序固定为：名称校验通过后先按本节 `tc.project` 规则做不写入预检，再预检
  `tests`；之后依次确保 `src`、`tests`、`tests/smoke` 三个目录
  （缺失才 `mkdir`；已有真实目录复用；`src`/`tests/smoke` 为 symlink 或非目录时跳过其
  后代，`tests` 已在预检中拒绝），再依次处理 `tc.project`、`src/main.tc`、
  `tests/smoke/main.tc`、`tests/smoke/stdout.bin`、`.gitignore`、`.gitattributes`。
  每个文件只在缺失时按 `open(O_CREAT|O_EXCL|O_NOFOLLOW)`、完整 write、close 的顺序创建；
  已有文件或 symlink 只记 skipped，不打开。首个写入/close 失败即停止后续创建并回滚。
  stdout 的 created/skipped 行只按上述六个文件顺序输出，不把新建目录作为独立目标输出。
- 不创建 `build/`，不调用编译器，不写任何绝对路径。
- 输出：仅在全部创建、写入与 close 成功、无需回滚后，先打印 `initialized project "<name>"`，
  再按固定目标顺序打印 `created`/`skipped` 的**相对路径**，最后打印 `next: tc --test`；
  不含 cwd 绝对路径。任一步失败并回滚时 stdout 必须为空，错误原因写 stderr。
- 退出码：0 成功（含 skipped）；2 已有工程/名称非法/`tests` 非真实目录；70 IO/环境；71 内部。
- 写入失败：删除本次新建的文件及本次新建的空目录，保留原有文件/目录，退出 70；按 create/write/close
  故障注入验证完整树差异，部分 close 失败也不得留下半成品；外部 symlink 目标不变。
  init 假定 cwd 及新建路径在整个操作期间由调用者独占，不存在并发改名/替换；回滚的
  身份保证以每次复核时刻为界，不提供目录锁，也不承诺阻止复核后的并发替换。
  回滚按上述规范创建顺序的逆序逐项执行，每次 unlink/rmdir 前按创建身份复核，复核时
  身份已被替换的条目不删除。单项回滚失败仍继续尝试其它条目，但不升级为更强的删除操作；
  回滚自身的 unlink/rmdir/身份复核失败不改变 70。stderr 固定逐行报告未能确认清除的
  根相对路径 `cleanup failed: <path>`，路径顺序为创建顺序；同一路径只报告一次。没有
  未能确认清除的条目时不输出该行。回滚完成后 stdout 仍为空，stderr 只含初始错误与
  上述残留错误。
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

- 发现与验证分为两阶段：先只枚举 `tests` 的直接目录项并收集入口名，不 canonical 化单个测试，
  不因枚举顺序产生错误；按筛选规则得到选中集合后统一按测试名字节序排序，再逐个验证入口、
  canonical 边界与 `main.tc`。首个验证错误即停止，错误与退出码不依赖 `readdir` 顺序。
  仅发现 `tests/*/main.tc`（一层）。`tc --test` 只读，发现与读取路径可跟随
  `tests` 树内 symlink；这与 `--init-project` 的写路径"不跟随、不写穿"规则不同，
  二者按各自语义执行。测试输入的 canonical 目标边界：`tests` 必须是工程根内的真实
  目录（symlink → 2）；`tests/<name>` 必须在 canonical `tests` 树内；`main.tc` 与全部侧车
  必须在所属 `tests/<name>/` 内。本文的"所属测试目录"指入口 `tests/<name>` 的 canonical
  目标目录（入口是 symlink 时取解析后的目录）。发现阶段先 canonical 化 `tests/<name>`，
  越界一律退出 2（即使 `main.tc` 不存在），不读取目标；canonical 化因目标不存在
  （dangling symlink）失败时按测试布局错误退出 2，不读取目标；因 symlink 环或其它解析
  错误失败时退出 70；`tests/<name>` 是普通目录但 `main.tc` 不存在仍按非测试目录跳过。
  对应测试不执行（发现阶段越界则整体不执行）；报告只使用根内入口路径，不打印外部目标
  绝对路径。外部 fixture 必须复制
  或链接到对应测试目录内；不得指向测试目录外。0.0.47 不提供越界读取开关。`tests` 的 `lstat`
  返回 ENOENT/ENOTDIR → 无测试目录；返回其它错误 → 退出 70；条目存在时必须是真实目录：
  任何 symlink（无论目标）或非目录 → 退出 2，不解析目标；目录枚举失败 → 退出 70。
- `tests/<name>/main.tc` 的 `lstat` 返回 ENOENT/ENOTDIR → 非测试目录；返回其它错误 →
  退出 70；存在即视为测试目录（名字取入口目录名）。测试目录 canonical 目标在 `tests` 树
  外，或 `main.tc` canonical 目标在所属测试目录外 → 退出 2；测试目录内的 `main.tc` 必须
  是可读普通文件，`stat` 失败、dangling symlink、目录、FIFO 或读取失败 → 退出 70，不当作
  测试失败。
  其它子目录忽略。选中入口名必须是合法 UTF-8 且不含 Unicode Cc/Cf/Zl/Zp，否则退出 2
  （避免不可解析或被伪装的报告）。边界验证与后续打开必须锚定同一 canonical 测试目录
  fd：从该 fd 打开 `main.tc`，允许的 symlink 解析不得越出锚定目录，并对同一 fd 做
  `fstat` 普通文件/身份复核；不能校验后重新按裸路径 open。发现、预检、侧车读取与 worker
  加载均使用该锚点，使复核后发生的目录替换不改变已绑定身份。
  按入口名的 UTF-8 字节序（逐字节无符号比较）排序后，
  才执行上述 `tests/<name>` canonical 化与入口验证；同一入口通过类型/边界验证后立即执行
  下一条的 `#program`/`#lib` 模式预检，再处理下一个名称，不把所有入口验证完后再统一预检。
  普通目录缺少 `main.tc` 时跳过该名称，继续验证下一个名称。因此 `a/main.tc` 超限 71
  先于 `z` 的 dangling 目录 2；筛选参数与入口名校验均先于 header，未知名称或选中名称
  非法都不执行测试。
- 测试名 = 目录名，按字节精确匹配。筛选的“已知名称”集合是 `tests` 直接枚举且通过
  名称校验的子目录名与 symlink 名；普通空目录或缺少 `main.tc` 的目录也是已知名称，但选中后按非测试
  目录跳过。因此显式筛选一个空目录可成功得到 0 tests，不按未知名称退出 2。
  无参数执行全部，带参数筛选（重复名称只执行一次），筛选后仍按同一顺序执行；所有筛选
  参数先校验，任一不属于已知名称集合 → 退出 2 且不执行任何测试。
- 全部选中测试在打印 header 前按测试名字节序逐个做入口模式预检，首个错误停止：
  先以 `open`+`fstat` 确认入口仍是普通文件且不超过 4 MiB（超限 → 71），再按源码首个有效行
  （跳过空行与注释）判定 `#program`/`#lib`。首个判定为 `#lib` 的入口按测试布局/用法错误
  退出 2，不打印 header、不执行任何测试、零 worker。预检只读取模式行，且模式行读取本身
  最多 4 MiB+1；模式行读完后对同一 fd 再做一次 `fstat`，大小增长至超过 4 MiB 即退出 71，
  之后仍由 worker 按普通源文件读取规则复核。预检的 `main.tc` 打开/读取失败按本节 70，
  读取超限按本节 71；侧车仍按 §6.3 读取，错误时序按 §6.4（header 后）。`#lib` 与后续
  LEX/SYN 错误并存时，以 `#lib` 布局错误 2 优先；模式之前的 LEX/SYN 错误或无法确定模式时，
  交 worker 按 §6.4 的编译诊断处理（测试失败 1）。预检后文件被替换且 worker 发现首个有效
  模式变为 `#lib` 时按内部/协议错误退出 71，不映射为测试失败 1；入口已是 `#program` 后
  出现的重复 `#lib` 属于普通 SYN 诊断，仍计测试失败 1。
- `tests` 的 `lstat` 缺失（按本节首条判定）：无筛选参数时 0 个测试，退出 0 并打印
  `0 tests`；带筛选参数时所有名称均为未知，退出 2。目录枚举/读取失败退出 70；测试源
  读取失败退出 70，不当作测试失败。

### 6.2 侧车文件

| 文件 | 含义 | 缺省 |
| ---- | ---- | ---- |
| `stdout.bin` | 期望 stdout 精确字节 | 空 |
| `stdin.bin` | 喂给程序的 stdin 字节 | 空 |
| `trap` | 期望首个 `TC_RE_*`（ASCII 且仅 `[A-Za-z0-9_]`，不含空白；去掉至多一个 LF/CRLF 行尾后按字节精确匹配） | 无 trap |
| `exit` | 期望退出码（ASCII 无符号十进制，0..255；可带一个 LF/CRLF 行尾） | 有 `trap` 时为档案 trap 状态（当前 1），否则 0 |

- 同一测试的侧车严格按 `stdout.bin`、`stdin.bin`、`trap`、`exit` 顺序校验；每个文件依次
  做缺失判定、canonical/边界、类型与可用性、读取/格式检查，首个错误立即停止，不继续
  后续侧车。因此不同侧车的双错固定由该顺序决定；同一侧车的 canonical 越界 2 优先于
  dangling/类型/读取 70，格式 2 只在该侧已通过前序检查后判定。`trap` 的缺省值依赖其
  自身是否存在，但 `exit` 缺省值同时依赖 `trap` 的期望是否存在，不依赖实际运行结果。

- `stdout.bin`/`stdin.bin` 是二进制精确字节；不使用文本换行转换。`tc --test` 只读，侧车
  路径可跟随 `tests` 树内 symlink，但 canonical 目标必须在所属测试目录内，且解析后必须
  是普通文件。侧车 `lstat` 返回 ENOENT/ENOTDIR → 取缺省值；返回其它错误 → 退出 70；
  条目存在时先按允许目标不存在的规则解析 symlink 链：若预期目标已在所属测试目录外，
  退出 2，优先于 dangling/不可用 70；目录内 `stat` 失败（dangling symlink、ELOOP）、
  解析后非普通文件（目录、FIFO、设备等）或打开/读取失败 → 退出 70。文件均从 §6.1 的
  canonical 目录 fd 打开并做 `fstat` 类型检查；允许的 symlink 解析不得越出锚定目录，
  不能校验后重新按裸路径 open。格式错误同样退出 2。
- `trap`/`exit` 是小型文本侧车；单个文件读取上限 4 KiB，超过上限按格式错误退出 2
  （`stdout.bin`/`stdin.bin` 不受此限，分别按流式比较与文件描述符处理）。
- `trap`/`exit` 只去掉至多一个行尾 LF 或 CRLF；其余空白不 trim。`trap` 含任何空白、
  `exit` 含前后空白均按形式错误退出 2；保留的空白不得作为可比较的期望值。
- `trap` 必须是 ASCII 单行文本，形式为 `TC_RE_[A-Za-z0-9_]+`；去掉至多一个行尾后不得
  为空；拒绝 NUL/其它 Cc/Cf/Zl/Zp/空白；比较按字节长度而非 NUL 终止；格式错误退出 2；
  报告中的期望值只含 ASCII 字母、数字与下划线。
- `exit` 必须是 ASCII 无符号十进制 0..255；不接受符号、前后空格、多于一个行尾或其它字符。
- `trap`/`exit` 格式错误（空 `trap`、非 `TC_RE_`、含非 ASCII/控制字符/空白、非法 `exit`、
  额外行尾、行内换行或超过 4 KiB）→ 退出 2；该测试不执行，此前已执行测试的报告保留；
  这不是测试失败，也不静默忽略。
- `trap` 缺省表示期望正常终止；`exit` 显式给出时必须与运行结果一致。VM 正常终止为 0，
  trap 为档案 trap 状态（当前 1）；其它值必然失败，不作为用法错误。
- `trap`/`exit` 的实际值取自 worker 的运行结果（首个 trap 码与退出状态），不扫描
  stdout/stderr；程序输出中的 `TC_RE_*` 文本不参与判定。
- 测试目录本身是 include 根（优先于 manifest 的 `include`，import 仍先查导入者目录），
  再叠加 manifest 的 include 顺序。测试目录是 symlink 时使用其解析后的 canonical 目录
  作为 include 根；该目标必须在 `tests` 树内；测试名仍取入口名。
- 测试入口必须能通过静态阶段；编译诊断按普通诊断格式输出，测试计为失败。
- `#lib` 根不是合法的测试入口：它是实现/API 错误，不产生语言诊断，测试 runner 统一按
  测试布局/用法错误退出 2，不得映射为普通测试失败 1；该判定由 §6.1 的模式预检在 header
  前完成，worker 若在预检后因文件被替换而发现首个有效模式为 `#lib`，按内部/协议错误
  退出 71；`#program` 后的重复 `#lib` 仍按普通 SYN 诊断计失败 1。`#program` 入口仍按
  编译诊断失败 1。

### 6.3 执行

- 每个测试由一个私有 worker 执行：父进程 fork 后，worker 复用私有 core 的加载与
  `tc_vm_run`，不 exec `tc-vm`，不调用 AOT/clang/lld，不导出公共 API。无论是否设置
  超时都使用 worker；测试按名称顺序串行执行。
- 父进程确定工程根、profile 与 include 顺序后传给 worker；worker 不重新从测试源路径
  做根发现。worker 的 cwd = 工程根；stdin 始终来自 `stdin.bin`（缺省为空，立即 EOF），
  不继承调用者终端；stdout/stderr 由父进程捕获。stdout 与 `stdout.bin` 比较；stderr 不
  参与通过判定，仅作为失败详情输出。每个测试使用全新的 frontend/IR/CT/VM 状态。
- 父进程在 fork 前读取并校验当前测试的侧车；缺失取缺省值；canonical 目标在所属测试
  目录外或格式错误退出 2；目录内存在但不可用或读取失败退出 70；2/70 均停止后续测试并
  保留已输出报告。deadline 不覆盖父进程的侧车读取与结果比较。
- 默认不设 deadline；`tc --test --test-timeout=<seconds>` 可选设置单测试 deadline。
  `<seconds>` 只含 ASCII 十进制数字、非负、不接收符号/空格/小数，按 uint64 解析，超过
  `UINT64_MAX` 视为溢出并退出 2；缺省或 `0` 表示不限，正数从 worker 开始执行时计时，
  覆盖加载、CT/IR 与 VM 执行；deadline 换算饱和，不因乘法溢出提前到期。父进程用单调
  时钟计时；到期后向 worker 发送 SIGKILL 并回收，测试计为失败（退出 1），报告稳定的
  timeout 文本并继续后续测试。超时前 stdout 继续排空并丢弃，不进入失败详情，也不作为
  通过依据。
- `stdin.bin` 以只读文件描述符直接作为 worker stdin（或等价地并发写入），不因大输入
  阻塞父进程的 stdout/stderr 排空。
- 父进程并发排空 stdout/stderr；stdout 与 `stdout.bin` 流式逐字节比较，比较始终使用
  完整字节，并记录首个差异位置及总字节数。为失败详情只保留首个差异前至多 32 KiB 与
  差异后至多 32 KiB；仅长度不同而在 EOF 发现差异时保留期望/实际末尾至多各 32 KiB。
  更早字节在首个差异确定前用固定 32 KiB 滑动窗口淘汰，差异确定后不再扩展上下文。
  stderr 只保留前 64 KiB 与总字节数，其余继续排空后丢弃。捕获上限不改变通过判定。
- 失败详情的固定 stderr 格式如下；`<test>`、`<reason>` 与 stdout 的 `FAIL` 行完全相同。
  方括号段是否出现由对应失败类型决定，原始字节不转义、不换行转换；每个原始段后由父进程
  固定追加一个 LF，即使原始字节已以 LF 结束。`<want>`/`<got>` 的 trap 值为 `none` 或
  实际 trap 符号，exit 值为 ASCII 十进制退出状态。

  ```text
  detail <test>: <reason>
  [result]
  trap: expected <want>, got <got>
  exit: expected <want>, got <got>
  [stdout]
  first difference at offset <k>, expected <n> bytes, got <m> bytes
  --- expected stdout [<start>,<end>) ---
  <原始字节>
  --- actual stdout [<start>,<end>) ---
  <原始字节>
  [stderr]
  --- stderr [0,<kept>) ---
  <原始字节>
  ```

  普通运行失败始终有 `[result]` 段；stdout 不一致时还有 `[stdout]` 段；stderr 非空或
  编译失败时有 `[stderr]` 段。compile 失败无 `[result]` 段；timeout 只有 detail 行与
  非空 `[stderr]` 段，不输出超时前 stdout。stdout 上下文区间为 `[start,end)`，期望与
  实际分别计算：普通字节差异对长度为 `L` 的一侧使用
  `start = min(max(first_difference-16384,0), max(L-32768,0))`、
  `end = min(start+32768,L)`，在 32 KiB 窗口内尽量保留差异两侧上下文；仅长度不同
  而在 EOF 发现差异时，两侧统一使用 `start = max(0,L-32768)`、`end = L`，保留各自末尾。
  因此普通差异与 EOF 差异两侧区间都可以不同。每个区间若 `start < end`，区间后省略 `L-end`
  字节并追加 `... truncated: <L-end> bytes omitted`；stderr 保留 `kept = min(total,65536)`，
  非空时原始段后省略 `total-kept` 字节并追加
  `... truncated: <total-kept> bytes omitted`。省略数为 0 时不输出截断行；每个截断行
  以一个 LF 结束。stdout 两个上下文最多各 32 KiB，合计最多 64 KiB；固定标签、结果行与
  截断行另计，每个失败测试的固定报告结构允许超过 128 KiB。失败详情整体仍按执行顺序写
  stderr，编译诊断作为 `[stderr]` 原始段的一部分，不另加诊断包装。
- TC 生成的路径字段 root 相对，不含绝对路径；测试程序输出本身按原字节保留（报告截断
  除外），不做路径改写。
- 不生成 build 产物；不向 `tests/` 写文件。worker 的管道/临时捕获文件位于工程外，
  其宿主路径不进入报告或诊断。

### 6.4 输出与退出码

```text
project "demo" (aarch64-linux-user)
FAIL tests/exit: trap expected TC_RE_DIV_ZERO, got none
ok   tests/smoke
1 passed, 1 failed
```

示例只展示 stdout；失败详情按 §6.3 的固定模板另在 stderr。

- 报告中的 `<test>` 是逻辑测试路径 `tests/<目录名>`；筛选、排序与报告用它，源位置/失败
  诊断仍用 §4.3 的 display 路径。
- stdout 每个测试一行报告：`ok   <test>` 或 `FAIL <test>: <reason>`，按执行顺序输出，
  即测试名字节序；不缓冲后排序。完整的 stdout 差异、stderr 原文与 trap/exit 细节写
  stderr，也按执行顺序输出；跨流相对顺序不构成契约。`<reason>` 只从下列固定形式取值，
  同一测试多项失败时按表中优先级取第一项；非 compile error/timeout 的详情仍列出全部
  result 字段与 stdout/stderr。compile error 与 timeout 分别按 §6.3 模板，不列出未产生的
  退出/trap 实际值；timeout 不展示已丢弃的 stdout，但 stderr 按前 64 KiB 保留，非空时
  仍输出 `[stderr]` 段。

  | 优先级 | `<reason>` | 判定 |
  | --- | --- | --- |
  | 1 | `compile error` | 加载/LEX/SYN/静态阶段产生语言诊断 |
  | 2 | `timeout after <seconds>s` | worker 到期被 SIGKILL，`<seconds>` 为参数原文 |
  | 3 | `trap expected <want>, got <got>` | 首个 trap 符号不同；无 trap 记为 `none` |
  | 4 | `exit expected <want>, got <got>` | trap 相同但退出状态不同 |
  | 5 | `stdout mismatch` | trap/exit 均相同但 stdout 字节不同 |

  compile error 与 timeout 互斥；普通运行失败没有第六种自由文本原因。退出/trap 的实际值
  只取 §6.2 的 worker 运行结果，不扫描输出文本。
- 报告是面向人的文本，不提供机器解析语法；脚本以退出码和逐字节 golden 为准，不解析
  `<test>` 字段。
- header `project "<name>" (<profile>)` 在 manifest/根解析、测试发现、入口验证、筛选校验
  与全部选中入口的模式预检通过后、执行第一个测试前打印一次。0 个测试时在其后打印
  `0 tests`，不打印 passed/failed 行。
  CLI 用法错误、未知筛选名、`tests` 布局错误（含 §6.1 的 `#lib` 模式预检与 dangling
  测试目录）发生在 header 之前，不打印 header；执行期的侧车/IO/内部错误发生在 header
  之后，header 已打印。
- 正常执行完所有选中测试后打印 `<n> passed, <m> failed`；因 2/70/71 提前终止时不打印
  汇总行，已输出的 ok/FAIL 行保留，终止原因写 stderr。0 个测试用 `0 tests` 代替汇总行。
- 成功：0；有失败：1；用法/无工程/未知测试名/`tests` 布局错误、侧车格式错误或侧车目标
  在所属测试目录外：2；环境、IO 或内存耗尽：70；内部：71。stdout/trap/exit/编译诊断/
  timeout 失败计 1 并继续后续测试；2/70/71 停止。
- timeout 报告为 `FAIL <test>: timeout after <seconds>s`；`<seconds>` 是参数原文，不打印
  实际耗时或 PID。timeout 计 failed，后续测试继续。
- fork 失败或 worker 返回源读取/环境 IO/内存耗尽错误：退出 70；worker 路径停止后续测试
  并保留已输出报告。worker 报告语言/静态诊断时按普通诊断格式输出，测试计为失败（退出 1）；
  非超时的 worker 异常终止、内部错误或结果协议损坏：退出 71，停止后续测试并保留已输出
  的报告。
- 报告与汇总写 stdout；编译诊断与失败详情写 stderr；TC 生成的路径字段没有绝对路径、
  颜色或时间戳。测试程序 stdout 按原字节与 `stdout.bin` 比较；stderr 按原字节保留（受
  §6.3 捕获上限约束），只在失败详情中输出；0.0.47 没有 stderr 侧车，二者都不做路径改写。
  成功测试的 stderr 不输出。失败详情按 §6.3 的上限截断，截断标记不改变退出码。

## 7. `tc` CLI

```sh
tc --init-project [--] project_name
tc --test [--test-timeout=<seconds>] [--] [test_name...]
tc --help
tc --version
```

- `tc` 不编译、不运行用户 entry；`--test` 内部编译/运行的是测试入口；用户程序仍由
  `tc-vm`/`tc-aot` 编译。操作必须是第一个参数；操作之前出现任何参数（含 `--`）→ 2。
  `--test-timeout` 只属于 `--test`，必须出现在 `--test` 之后；与 `--init-project` 同时
  出现、出现在 `--test` 之前或单独出现 → 2；未给出时不设单测试超时；只接受
  `--test-timeout=<seconds>` 形式。
- `--init-project` 与 `--test` 不能同时出现；`--help`/`--version` 必须单独使用，与任何
  工程操作或其它参数同时出现 → 2；未知参数/多余参数/没有操作 → 2。
- `--init-project`、`--test`、`--help`、`--version`、`--test-timeout` 不得重复；违反 → 2。
- `--` 结束选项解析，其后参数即使以 `-` 开头也按位置参数处理；`--` 只能出现在已识别的
  `--init-project` 或 `--test` 之后，且至多一次；操作之前出现 `--`（如
  `tc -- --init-project x`）或重复 → 2。`--` 之后的 `--` 是位置参数，不受此限。`--test`
  之后的选项与测试名可任意顺序，直到 `--` 为止。
- 未来 `--build`/`--run` 若加入，复用 `entry`/`profile`；0.0.47 不实现。

## 8. 实现落点

| 模块 | 内容 |
| ---- | ---- |
| `src/driver/tc.c` | `tc` 入口、`--init-project`/`--test`/`--help`/`--version`、`--test-timeout` 解析、退出码 |
| `src/project/manifest.[ch]` | `tc.project` 解析、根发现、字段校验、默认值 |
| `src/project/init.[ch]` | 模板与原子写入、名称校验（Cc/Cf/Zl/Zp）、skipped 报告 |
| `src/project/test_run.[ch]` | 测试发现、侧车缺失/不可用/格式校验、父进程调度/捕获/超时/回收、流式比较、捕获上限与报告截断 |
| `src/project/test_worker.[ch]` | fork 后的 worker 入口、fd/stdio 捕获、结果协议、`_exit` 与 FD 清理 |
| `src/base/path.[ch]` | root/canonical/display、外部 `../`、`./<basename>#<n>` 回退编号、路径字符校验、路径一致性 |
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
| `tc` CLI：init/version/help/退出码、init 的已有 `tc.project` 写入前拒绝与 `tests` 真实目录预检、`--test-timeout` 取值、操作顺序、操作前 `--` 与组合参数 | `tests/project/tc_cli_test.py` | `toolchain`, `fast` |
| manifest：语法、引号内 `#`、空值、CRLF、默认值、大小上限 4 MiB、检查顺序（超限+非法 UTF-8 71、open 失败+超限 70）、读取最多 4 MiB+1 并复核 EOF、根发现（含不可读/损坏 manifest 不跳过、root ENOENT/非普通文件或 canonical ELOOP/ENOTDIR/EACCES/EIO 先于 manifest 为 70，已有 root 普通文件则先报 manifest）、非法项 | `tests/project/manifest_test.py` | `toolchain`, `fast` |
| 工程测试 runner：`tests` 真实目录、测试目录在 `tests` 树内、`tests` 树外/跨测试目录 2、dangling `tests/<name>` 2/ELOOP 70、收集后按测试序验证（反转目录创建顺序与枚举顺序均同一首错）、`#lib` header 前模式预检（超限 71、读后复核增长、按测试序首错停止、无限循环前置测试不执行、零 worker）、`#lib` 根与 `#program` 后重复 `#lib` 的优先级、预检 70+2 按测试序首错、侧车固定校验顺序与跨侧车双错优先级、侧车缺失/不可用/格式、越界 dangling 侧车优先 2、trap 字符集/空白/字节比较、过滤预校验、header/0 tests/中止输出、固定 FAIL 原因优先级与失败详情逐字节 golden（含二进制上下文、EOF 两侧独立区间、两个 32 KiB 窗口与 stderr 64 KiB 截断）、worker 隔离、timeout、stdin EOF/大输入、stderr 判定、捕获上限与报告截断 | `tests/project/test_runner_test.py` | `toolchain`, `fast` |
| init 失败回滚：按规范 mkdir 与 create/write/close 顺序注入失败，断言 stdout 为空、原有树字节不变、本次新建文件/目录全部清除、外部 symlink 目标不变；另注入回滚 unlink/rmdir 与身份复核失败，断言仍为 70、stdout 为空、按创建顺序报告实际残留且不删除复核时已被替换身份 | `tests/project/init_fault_test.py` | `toolchain`, `fast` |
| 路径：诊断/trap/IR/trace/linemap、`module`/`callee_module`（含 external/`../`/`./<basename>#<n>`、空格/`]`、根内 `lib.tc#1` 与回退 `./lib.tc#1` 消歧）、include 根不存在/空候选、include 根存在但非目录、dangling/ELOOP 或非目录前缀 `ENOTDIR` 为 2（普通文件/FIFO/symlink-to-file 等）；`EACCES`/`EIO` 为 70、`#lib` 测试入口 2、AOT 汇编字符串、部分检查模式、路径字符（Cc/Cf/Zl/Zp）、root 非普通文件 70、import 首个解析目标非普通文件 70 且不跳过后续根、源文件 4 MiB 上限及读取期间增长至超限 71（不解析已读内容）、impl/IO 错误 | `tests/project/path_test.py` | `toolchain`, `fast` |
| monitor 路径：`monitor funcs`/`frames`/`trap` 的 module、`monitor vreg` 的 module 选择器（external/`../`/`./<basename>#<n>`、空格/`]`） | `tests/conformance/tools/`（协议用例；客户端 `tests/tools/vm_gdb_client.py`；待实现） | `toolchain`, `fast` |
| AOT 路径：ELF/flat 源字符串与板端 trap 记录 | `tests/project/path_aot_test.py` | `toolchain`, `aot` |
| 跨根可复现 asm/ELF/flat | `tests/determinism/project_determinism.py` | `toolchain`, `slow` |
| 三入口/release audit | `tests/tools/s10_release_audit.py`、`tests/tools/cli_contract_test.py` | `toolchain` |
| harness adapter 的 root 解析 | `tests/adapters/`、`tests/harness/` | `conformance` |

### 9.2 验收

- `tc --init-project` 在临时目录生成完整工程；生成文件无绝对路径；`.gitattributes` 覆盖
  `*.bin`；smoke 通过 `tc --test`。
- 已有 `tc.project`（含 symlink）时 init 在 `tests` 预检与任何 mkdir/写文件前退出 2，
  原树字节不变；非法名称（含 Cc/Cf/Zl/Zp）、非空目录、已有文件、最终/中间 symlink、
  权限失败均有确定行为；init 的 mkdir 与 create/write/close 故障注入必须完整回滚，保留原树、清除本次
  新建文件/空目录、外部 symlink 目标不变，退出 70；`src` 为 symlink 时 skipped、不写穿；`tests` 为 symlink/非目录时
  退出 2、不写任何目标，外部目标保持不变；回滚自身失败时按创建顺序报告实际残留，
  不因清理失败改变 70 或输出成功前缀；
  `tc --init-project -- -demo` 可初始化以 `-` 开头的合法名称；含其它 `.` 的名称
  （非 `.`/`..`）可初始化。
- `tc` 的操作必须是第一个参数；`tc -- --init-project x`、`tc --test-timeout=5 --test`
  退出 2；`tc --test -- --foo` 把 `--foo` 当测试名，未知时退出 2。
- manifest 的版本/未知键/重复键/空值/绝对路径/非法转义/非法 profile/非法缺省名称全部
  退出 2；读取/IO 错误退出 70、超过 4 MiB 退出 71；检查顺序固定为 `open`→`fstat`→超限
  71→最多读取 4 MiB+1 并复核 EOF→UTF-8/字段 2，因此“超过 4 MiB + 非法 UTF-8”为 71、“open 失败（不可读）+
  超限”为 70，`fstat` 成功且超限时不再读取，读取期间增长或超限同样为 71；
  include 总数超过 32 退出 2；
  include 顺序与基准确定（manifest 根相对、CLI cwd 相对），import 仍先查导入者目录；
  引号内 `#`、引号外注释与 LF/CRLF 行尾按 §3.1 解析；版本只有 `1` 合法
  （`01`/`+1` 为 2），最后一行可无行尾；空文件/仅注释为 2。
- 最近 `tc.project` 为目录/dangling/不可读时退出 70，不跳过继续向外；`tc.project` 为
  symlink 时工程根为其所在目录；`tc.project` 为 FIFO/设备时退出 70。root 源文件 ENOENT、
  非普通文件或 canonical ELOOP/ENOTDIR/EACCES/EIO 与损坏 manifest 双错时固定先退出 70；
  root 已存在且为普通文件时不打开内容，先报 manifest 错误。
- 根模块首个有效模式为 `#lib` 时，`tc-vm`、`tc-aot` 与 `tc --test` 均退出 2，不产生语言
  诊断、不执行或发射产物；该判定先于模式行后的 LEX/SYN 错误。
- manifest `profile = "w32"` 在 `tc-vm`/`tc --test` 可用；在 `tc-aot` 退出 70 且不生成产物。
- 固定同一 source、profile 与 canonical include 序列时，从工程根、子目录或工程外调用
  `tc-vm`/`tc-aot`，诊断/trap/IR/trace/linemap、`module`/`callee_module`
  路径一致且根相对；CLI 相对 `--include` 以调用者 cwd 为基准，跨 cwd 验收必须先固定
  等价的 canonical include 序列；`monitor funcs`/`frames`/`trap` 的 module 与 `monitor vreg` 的 module
  选择器路径由 `vm.gdb.protocol` 覆盖；无 `/Users/`、`/home/`、`/private/` 等绝对路径；`--syntax-only`/`--sem-only`/
  `--static-only`、缺失源文件、root 源路径为 FIFO/设备（70）、include 根存在但非目录
  （2，不存在仍跳过）、import 首个解析目标非普通文件（70，不跳过后续根；全部无匹配报
  `TC_CE_IMPORT_NOT_FOUND`）与 impl/IO 错误同样不泄露绝对源路径；
  侧车/`tests` 布局错误路径也 root 相对；源读取/宿主 IO/内存耗尽为 70，单源文件超过
  4 MiB/实现内部/上限为 71，且 `tc`/`tc-vm` 一致。
  回退 `./<basename>#<n>` 在同一编译内稳定（诊断与 linemap/trace 一致），不同编译模式可
  不同；路径含非 UTF-8 或 Cc/Cf/Zl/Zp 时 CLI/manifest 为 2、缺省工程名/测试目录名按
  名称规则为 2、其它发现路径为 70。
- 两个不同根目录、等价 canonical include 序列的同一工程，asm/ELF/flat 逐字节一致；代码/ABI 语义不变。
- `tc --test`：stdout/stdin 精确字节、trap/exit 侧车、失败报告、过滤、无测试；测试目录
  作为 include 根先于 manifest 的 include，worker 不继承终端 stdin；stderr 不参与判定、
  成功不输出、失败时进入失败详情；显式
  `--test-timeout` 对不终止测试返回 1、报告 timeout 文本、丢弃超时前 stdout 且后续测试继续；缺省/`0` 不限；
  负数、带符号/空格/小数、非整数、溢出、缺值、单独出现或与 `--init-project` 同现退出 2；
  结束后无残留 worker/临时文件。发现阶段只收集名称，选中后按测试名字节序逐个验证入口；
  该入口通过类型/边界验证后立即做 header 前模式预检，再处理下一个名称。因此
  `a/main.tc` 超限 71 先于 `z` dangling 2；反转目录创建/枚举顺序不改变首错。预检先检查
  4 MiB（超限 71）并在模式行读后复核增长；前置无限循环测试不执行、不打印 header、零 worker；
  dangling 为 2，ELOOP 为 70；FAIL 原因优先级与 stderr 详情模板逐字节按 §6.3/§6.4，
  含普通差异双向上下文窗口与 EOF 长度差异各自末尾窗口；
  `#lib` 优先于后续 LEX/SYN，`#program` 后重复 `#lib` 为 SYN 失败 1，模式前的 LEX/SYN 仍为 1。
  普通空目录和缺少 `main.tc` 的目录属于已知筛选名；显式筛选空目录成功打印 header 与
  `0 tests`，不按未知名称退出 2。对已绑定 canonical 测试目录做原子替换，并注入 main.tc/
  侧车在裸路径复核后替换；发现、预检与 worker 仍读取原绑定身份，不能读取替换目标或
  改变边界判定。
- `tc --test` 的 0 tests 打印 header 与 `0 tests`；2/70/71 提前终止不打印汇总行；侧车缺失
  取缺省，存在但不可用/读取失败 70；`tests` 必须是真实目录，测试目录必须在 `tests` 树
  内，`main.tc`/侧车必须在所属测试目录内；越界 symlink 退出 2、不读取目标内容；越界
  dangling 侧车同样优先退出 2，目录内 dangling/ELOOP 才为 70。
- `tc --test` 的侧车格式错误（空/非 `TC_RE_`/含非 ASCII、控制或空白的 `trap`、非法 `exit`、
  额外行尾、超过 4 KiB）退出 2 并停止后续测试，出错测试不执行；四个侧车按
  `stdout.bin`、`stdin.bin`、`trap`、`exit` 顺序校验，以 `stdin.bin` 不可用 70 与空 `trap`
  2 的双错固定为 70；`tests` 为普通文件、
  测试名含 Cc/Cf/Zl/Zp 或非法 UTF-8 退出 2；未知筛选名在 fork 前整体拒绝；`--` 后以
  `-` 开头的目录名可按测试名执行；程序输出中的 `TC_RE_*` 文本不被当作 trap 结果。
- `tc --test` 的大 stdout/大 stdin 不因管道阻塞；捕获按 §6.3 在首个差异前后各保留
  32 KiB 滑动窗口、stderr 保留 64 KiB，EOF 差异保留末尾；失败详情按固定模板逐字节输出，
  原始上下文合计最多 64 KiB；fork 失败退出 70；worker 非超时异常终止
  或结果协议损坏退出 71，且不产生伪通过。
- gdb 可用时从工程根按源码行断点；调试 ELF 的 `.file` 根相对；gdb 缺失按工具链
  §7.1/§8 显式跳过并记录原因。
- `tc --version`、三入口 release audit、AGENTS/README/docs map 同步。

## 10. 风险与限制

| 风险 | 处理 |
| ---- | ---- |
| 根发现依赖最近 manifest | 嵌套工程明确允许，最近祖先生效；文档写清 |
| 缺失源文件无法 canonical 化 | 用父目录/最近存在祖先的 canonical 路径作为根发现与显示起点；源读取仍 70，不泄露绝对路径 |
| root canonical ELOOP/ENOTDIR/EACCES/EIO、ENOENT/非普通文件与 manifest 双错 | canonical/非普通文件错误先退出 70，不发现或解析 manifest；ENOENT 可为显示根读取 manifest，但源缺失 70 始终优先；已有 root 普通文件则先报 manifest |
| 目录枚举顺序改变首错或 golden | 先收集选中名称，再按 UTF-8 字节序逐个验证；失败原因和详情模板固定 |
| init 回滚自身失败留下半成品 | 目录独占前提下按身份复核逆序清理；失败仍 70，stdout 为空，stderr 按创建顺序报告未确认清除路径；复核后并发替换不在保证范围 |
| 越界 dangling 侧车被误报 70 | 先按允许目标不存在规则解析；预期目标越界优先 2，目录内 dangling/ELOOP 才为 70 |
| init 失败仍打印成功前缀 | 全部写入/close 成功后才打印 stdout；回滚失败路径 stdout 为空 |
| 已有 `.gitattributes`/`.gitignore` 缺少规则 | init 不修改已有文件；工程需自行保留 `*.bin -text` 与 `build/` 忽略；新建文件场景由验收覆盖 |
| 根外 include 显示 `../` | 外部文件在行表标 `external:true`；不同卷回退 `./<basename>#<n>` |
| manifest 的注释/空值/换行歧义 | §3.1 固定引号内 `#`、空值非法、LF/CRLF；测试覆盖 |
| manifest 无界读取 | §3.1 单文件 4 MiB 上限，超限 71 |
| manifest 超限与非法 UTF-8 双错 | §3.1 固定 `open`→`fstat`→71→读取→2 顺序；不可读+超限为 70 |
| `profile` 与工具不兼容 | `w32` 仅 VM；`tc-aot` 退出 70，不回退 |
| 侧车格式错误被误报为测试失败 | 父进程在 fork 前校验；格式错误退出 2，停止后续测试 |
| `tests` 布局异常或测试名不可打印 | `tests` 非目录、测试名含 Cc/Cf/Zl/Zp 或非法 UTF-8 退出 2；不生成歧义报告 |
| dangling `tests/<name>` 被当作非测试目录 | 发现阶段 canonical 化失败：dangling 2、ELOOP 70；普通空目录仍跳过 |
| `#lib` 入口延迟到 worker 才报错 | header 前模式预检；预检后原绑定 fd 内容被改写为 `#lib` 时按 71，目录身份替换不得绕开锚点 |
| include 根存在但非目录 | 按字段/路径错误 2，不打开目标；不存在仍按空候选跳过 |
| 最近 `tc.project` 损坏或不可读 | 以目录项存在为准，最近锚点不跳过；70/2/71 退出，不回退外层工程 |
| 回退编号跨阶段漂移 | 首次进入显示路径时分配，同一编译内永不复用/重排；不同模式允许不同 |
| 路径含控制/格式字符或非 UTF-8 破坏文本或 JSON | 路径组件校验：CLI/manifest 与名称违反 2，其它发现路径违反 70 |
| root/import 源路径是 FIFO/设备 | root 与首个解析的 import 目标必须为普通文件，否则 70 且不阻塞；import 不跳过该目标改选后续根，仅目标不存在时继续，全部无匹配报 `TC_CE_IMPORT_NOT_FOUND` |
| 测试侧车或 `main.tc` 是 symlink/FIFO | 只读路径跟随 `tests` 树内 symlink；测试目录外 2；解析后必须是普通文件，不可用/读取失败 70 |
| `trap` 含 NUL/控制/空白/格式字符 | 只允许 `TC_RE_[A-Za-z0-9_]+` ASCII；按字节长度比较；否则 2 |
| 测试输入/配置的检查-打开竞态 | 项目目录视为可信；实现使用 open/fstat 或 openat/O_NOFOLLOW 等原子校验，不把 lstat-then-open 当唯一防线 |
| 大输出耗尽内存或磁盘 | stdout 流式比较并限量保留；stderr 限量捕获；trap/exit 单文件 4 KiB；失败详情截断并标记 |
| gdb 从非工程根启动找不到源 | 文档要求从根启动或设置 `directory`；T2/T6 验证 |
| AOT `.asciz` 改变产物字节 | 0.0.47 不承诺与 0.0.46 路径字节一致；代码/ABI 不变；跨根一致为验收 |
| 外部 clang/lld 诊断含绝对路径 | 属于外部工具输出，不纳入 TC 路径契约 |
| 测试侧车是二进制 | init 在缺少 `.gitattributes` 时生成 `*.bin -text`；已有文件不修改；0.0.47 落地时仓库 `.gitattributes` 同步添加该规则并纳入验收 |
| `tc` 复用私有 core 造成重复驱动逻辑 | 抽出私有 `run_source` 辅助，`tc` 与 `tc-vm` 共用；不导出 API |
| 侧车/工程目录写入失败 | init 回滚本次新建；test 不写源目录 |
| init 目标前缀是 symlink | `src` 等按 skipped、不写穿；`tests` 预检为真实目录，否则 2；验收检查外部目标不变 |
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

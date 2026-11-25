# TC 工程模型 0.0.47

本文按三层组织：§1–§7 是规范契约，规定可观察行为、错误优先级与输出；§8 是实现约束；
§9 只列验收证据，不重复契约；§10–§12 记录当前限制、里程碑与移交。

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
- 三入口的版本格式、共享退出码与 `TcImplError` 完整映射以[TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md)
  §3/§5.1 为唯一权威；工程工具不得新增退出码或另建映射。公共接口边界以该文件 §2 为准。
- 根模块首个有效模式为 `#lib` 时，`tc-vm`/`tc-aot`/`tc --test` 统一按实现/API 用法错误退出 2，
  不产生语言诊断、不执行或发射产物；该判定先于模式行后的 LEX/SYN 错误。

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
- 检查顺序固定为：非阻塞或等价 open → `fstat` 检查普通文件与大小 → 超过 4 MiB 退出
  71 → 最多读取 4 MiB+1 并复核 EOF → UTF-8/字段/路径解析退出 2。读取期间增长或超限
  同样为 71，不解析已读内容；宿主 IO 失败为 70。因此“超限 + 非法 UTF-8”为 71，“open
  失败 + 超限”为 70，“超限 + 未知键”为 71。编译器与 `tc --test` 共用该映射，不把无效
  manifest 静默当作缺省配置。

### 3.2 字段

| 字段 | 必需 | 说明 |
| ---- | ---- | ---- |
| `tc-project` | 是 | 格式版本；当前为 `1` |
| `name` | 否 | 工程名；缺省取工程根目录名。初始化工具写入命令行给的名称 |
| `entry` | 否 | 默认入口，缺省 `src/main.tc`；保留给未来的 `tc --build/--run`；0.0.47 的编译器仍接收显式 source |
| `include` | 否 | 可重复、有序的 import 根；相对工程根；缺省为 `src` |
| `profile` | 否 | 默认档案；缺省 `aarch64-linux-user`；必须是已实现档案；编译器 CLI 的 `--profile` 覆盖它 |

- CLI `--include` 根排在 manifest 的 `include` 之后（manifest 顺序优先，CLI 作为增量）。
  有效 include 根序列至多 32 个：manifest `include`、CLI `--include` 与缺省 `src`（仅当
  manifest 存在且未写 `include` 时加入）合计计数；字段解析完成后、任何 canonical 化或宿主
  路径访问前先计数，超出按字段/用法错误退出 2，不再访问后续根。因此 32 个 CLI
  `--include` 再加缺省 `src` 为 33，退出 2；无 manifest 时只使用 CLI 根，不加入缺省 `src`。
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
  单组件或完整路径超过宿主 `NAME_MAX`/路径上限（`ENAMETOOLONG`）同样按路径错误退出 2；
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
- 内部 `canonical_path` 只用于 import 解析与显示路径求相对，永不直接输出。模块实例键
  由“父目录实际路径 + 最终实际目录项名”组成：父目录身份来自文件系统/卷锚点与逐级实际
  目录项名，最终名从已绑定父目录 fd 枚举取得。大小写/Unicode 归一化文件系统的等价拼写
  归并，大小写敏感文件系统实际并存的 `A/m.tc` 与 `a/m.tc` 不归并；硬链接的不同目录项名
  仍按标准 §4.5 保持不同实例。不得全局 `casefold`/归一化，也不得用文件 inode 替代模块键。
  目录树归属与文件同一性必须用目录 fd 锚定和 `(st_dev,st_ino)` 或平台等价 beneath 机制判定，
  canonical 字符串不是安全判据。
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

- root 模块与 import 首个匹配的候选目录项必须最终解析为普通文件；目录、FIFO、设备、
  socket 或 dangling symlink 均退出 70，且不得阻塞打开。import 不因候选目标非法而改选
  后续 include 根；仅候选目录项确实不存在（`ENOENT`/`ENOTDIR`，且非 dangling symlink）
  才继续搜索，全部无匹配报告 `TC_CE_IMPORT_NOT_FOUND`。
- 候选存在性用目录 fd 上的 nofollow 元数据查询判定，不能仅凭 open 的 `ENOENT` 跳过
  dangling 目标；symlink 与 referent 普通文件不构成类型冲突。查询与打开必须绑定同一次
  候选解析：查询不存在而 open 成功时以 open 绑定候选为准；查询存在而 open 不存在，或非
  symlink 候选打开身份与查询不一致时，从同一目录 fd 重查并重试一次，仍不一致退出 70，
  不跳过候选。symlink 候选按打开的 referent 绑定模块身份。
- 单源文件上限 4 MiB：`fstat` 超限直接退出 71；否则最多读取 4 MiB+1 并复核 EOF，
  读取期间增长或超限同样退出 71，不解析已读内容。
- 实现以 `O_NONBLOCK` 打开后 `fstat` 或等价机制判型；已知非普通类型在 open 前拒绝，
  不能保证非阻塞 open 的设备在可信目录前提下于 open 前排除。不得把 `lstat`-then-`open`
  作为唯一防线。

无 `--trace-paths=abs`、`--debug-path-map` 一类绝对路径开关；0.0.47 不提供绝对输出模式。
外部工具（clang/lld）自身打印的绝对临时路径不属于 TC 输出契约，属于外部工具诊断。
`tc --test` worker 的 fork、管道与临时捕获文件只服务进程隔离；其宿主路径、PID 与实际
耗时不属于 TC 输出，报告只使用测试名和工程相对路径。

### 4.4 调试信息

- AOT `--debug` 的 `.file`/`.loc`/`# source:`/`.asciz`、DWARF v4、编译目录、共享行表与
  真实 gdb 格式消费验收，以[TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md) §5.8
  为唯一权威；跨根产物一致性条件也以该节为准。P4b/G11 证据归该文件 §6.10/§7.2，
  本文件不重复规定。
- 工程模型只约束上述调试路径值必须取 §4 的显示路径；VM 与板端 trap 使用同一显示路径。

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
- 名称校验与 `tc.project`/`tests` 预检的早期诊断先缓存。完成目标发现后、任何 mkdir、
  文件写入或诊断输出前，对 fd1/fd2 与六个目标（含 skipped 文件的可解析 referent）做身份
  联合检查；同一身份退出 2，不创建目录、不修改文件、不打印报告，也不把缓存诊断写入别名
  输入。fd 已关闭不构成别名，由写出阶段决定退出码；其它 `fstat` 错误在写入前为 70。最终
  组件为 dangling symlink 或 symlink 环时仍按 skipped，不因无 referent 改报 70。身份发现
  不改变已确定首错；无法证明 fd2 安全时丢弃缓存诊断，按原用法/环境错误退出。
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
- 规范顺序固定为：名称校验 → `tc.project` 不写入预检 → `tests` 预检 → 身份预检 →
  依次确保 `src`、`tests`、`tests/smoke` → 依次创建 `tc.project`、`src/main.tc`、
  `tests/smoke/main.tc`、`tests/smoke/stdout.bin`、`.gitignore`、`.gitattributes`。目录仅在
  缺失时 mkdir，已有真实目录复用；`src`/`tests/smoke` 为 symlink 或非目录时跳过后代，
  `tests` 已在预检中拒绝。文件仅在缺失时以 `O_CREAT|O_EXCL|O_NOFOLLOW` 创建并完整 write/close；
  已有文件或 symlink 只记 skipped。首个写入/close 失败停止后续创建并回滚；stdout 的
  created/skipped 行只按六个文件顺序输出，不把目录作为独立目标。
- 不创建 `build/`，不调用编译器，不写任何绝对路径。
- 输出：仅在全部创建、写入与 close 成功、无需回滚后，先打印 `initialized project "<name>"`，
  再按固定目标顺序逐行打印 `created <path>` 或 `skipped <path>`（路径为根相对），最后打印
  `next: tc --test`；不含 cwd 绝对路径。任一步创建/写入失败并回滚时 stdout 必须为空，
  错误原因写 stderr。
  报告写在全部文件提交之后，init 全生命周期忽略/捕获 `SIGPIPE` 并检查 write/flush/close；
  报告写出失败退出 70，允许已提交文件与不可撤回的部分 stdout 保留，不再回滚文件，也不得
  打印成功汇总。stderr 也不可写时仍完成清理并保持 70。
- 退出码：0 成功（含 skipped）；2 已有工程/名称非法/`tests` 非真实目录；70 IO/环境；71 内部。
- 写入失败时按创建顺序的逆序回滚本次新建文件与空目录，unlink/rmdir 前复核创建身份；
  复核时已被替换的条目不删除。单项失败仍继续，但不升级为更强删除，也不改变退出 70。
  stderr 按创建顺序逐行报告未确认清除的根相对路径 `cleanup failed: <path>`，同一路径只报
  一次；无残留不输出该行。回滚完成后 stdout 为空，stderr 只含初始错误与残留错误。
- cwd 为 symlink：canonical 化后写目标目录。

## 6. `tc --test`

### 6.1 发现

- 发现与验证分两阶段：先只枚举 `tests` 直接目录项并收集入口名，按筛选规则得到选中集后
  统一按测试名字节序排序，再逐个验证入口与 fd 身份边界；首个错误即停止，不依赖 `readdir`
  顺序。只发现一层 `tests/*/main.tc`，测试输入可跟随 `tests` 树内 symlink；写路径仍按
  §5 不跟随、不写穿。`tests` 必须是工程根内真实目录（symlink → 2）；`tests/<name>`、
  `main.tc` 与全部侧车必须由 fd 身份判定分别位于 `tests` 树与所属测试目录内，不得用
  canonical 字符串前缀替代。`tests` 的 ENOENT/ENOTDIR 视为无测试目录，其它错误或枚举
  失败为 70；条目存在但不是真实目录为 2。`tests/<name>` 越界一律为 2（即使 `main.tc`
  不存在），dangling symlink 或解析后非目录为 2，ELOOP 等解析错误为 70；普通目录缺少
  `main.tc` 则跳过。报告只使用根内入口路径，不打印外部目标绝对路径。
- 所有只读测试路径的 symlink 解析采用 beneath 语义：从已绑定锚点逐分量展开相对 symlink，
  任何中间分量（包括 symlink 目标与 `..`）一旦离开允许目录即拒绝，不因后续分量返回界内而
  接受。例如 `tests/a -> ../out/back -> ../tests/b` 在第一步越界时即为 2。绝对 symlink
  一律视为越界并退出 2，即使其目标位于 `tests` 内，也不得利用 `openat` 对绝对路径忽略目录
  fd；仅可使用 `openat2(RESOLVE_BENEATH)` 等等价机制。正常解析错误仍按本节的 2/70 规则，
  边界检查不读取越界目标内容。
- `main.tc` 的 ENOENT/ENOTDIR 表示非测试目录，其它错误为 70；条目存在即构成测试。
  测试目录或预期 `main.tc` 目标在允许边界外为 2；目录内 dangling、非普通文件或读取失败
  为 70。入口名必须是合法 UTF-8 且不含 Cc/Cf/Zl/Zp，否则为 2。验证与后续打开都锚定
  同一 `tests` fd 与测试目录 fd，以 `O_NONBLOCK` 打开、`fstat` 判型，不按裸路径重开。
- 发现/预检记录测试目录与 `main.tc` 的 `(st_dev,st_ino)`、可用文件世代和全文确定性摘要，
  随后关闭 fd，不长期占用全部测试 fd。执行前从同一 `tests` fd 重开并先复核目录、main
  身份/世代；变化时在读取前退出 71。身份相同时从偏移 0 有界读取并复核全文摘要，变化同样
  为 71，绝不加载替换目标。无不可复用世代时，同 inode 且逐字节相同的新项作为同一快照
  放行，不同内容拒绝。复核后 main 在父进程复制为工程外私有只读快照，worker 只读快照，
  源显示路径仍为原入口。
- worker 继承已绑定测试目录、main 快照、工程根与 include 目录 fd；相对 import 从已打开
  导入文件父目录经 `openat` 解析，侧车从同一测试目录 fd 打开，不使用可替换 cwd/裸路径。
- 每个入口通过类型/边界验证后立即做 `#program`/`#lib` 模式预检，再处理下一个名称。
  因此 `a/main.tc` 超限 71 先于 `z` dangling 2；筛选参数与入口名校验均先于 header。
- 测试名 = 目录名，按字节精确匹配。筛选的“已知名称”集合是 `tests` 直接枚举且通过
  名称校验的子目录名与 symlink 名；普通空目录或缺少 `main.tc` 的目录也是已知名称，但选中后按非测试
  目录跳过。因此显式筛选一个空目录可成功得到 0 tests，不按未知名称退出 2。
  无参数执行全部，带参数筛选（重复名称只执行一次），筛选后仍按同一顺序执行；所有筛选
  参数先校验，任一不属于已知名称集合 → 退出 2 且不执行任何测试。
- 父进程先绑定工程根 fd，再从该 fd 读取 manifest、枚举 `tests` 和打开 include 根。main 与
  侧车使用私有快照，但递归 import 不包含在快照保证内；并发改写 import/include 目录属于
  §10 的不受支持调用方式，不得宣称完整闭包已复核。
- 入口模式预检得出首个结果后、打印该结果或 header 前，对 fd1/fd2 与 manifest、选中测试
  的 main、四个侧车及容错 import 扫描目标做身份联合检查；同一身份退出 2，不打印 header、
  不执行测试。关闭的 fd 不构成别名，由写出阶段决定退出码；其它 `fstat` 错误在 header 前
  为 70。扫描只解析身份，不产生语言诊断，不改变模式预检首错，也不提前判定侧车格式。
  选项、根发现、manifest、发现与模式预检诊断先缓存；fd2 别名时静默退出 2，无法证明安全
  时丢弃缓存诊断，按原用法/环境错误退出。
- 每个选中入口在通过类型/边界验证后、header 前立即做模式预检，再处理下一个名称；
  首个错误停止。模式值与优先级以 §1 为唯一权威。以 `O_NONBLOCK` 打开并 `fstat` 确认普通
  文件且不超过 4 MiB（超限为 71），读取最多 4 MiB+1，按首个有效行判定
  `#program`/`#lib`，再对同一 fd 复核大小增长（超限为 71）。命中根 `#lib` 错误时不打印
  header、零 worker，其后的 LEX/SYN 不改变该优先级。模式前的 LEX/SYN 或模式无法确定时交
  worker 处理；`#program` 后的重复 `#lib` 仍为 SYN 失败 1。预检后身份/世代/摘要变化在
  执行前按上述规则为 71，不映射为测试失败，也不加载替换内容。
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
  做缺失判定、fd 身份边界、类型与可用性、读取/格式检查，首个错误立即停止，不继续
  后续侧车。因此不同侧车的双错固定由该顺序决定；同一侧车的越界 2 优先于
  dangling/类型/读取 70，格式 2 只在该侧已通过前序检查后判定。`trap` 的缺省值依赖其
  自身是否存在，但 `exit` 缺省值同时依赖 `trap` 的期望是否存在，不依赖实际运行结果。

- `stdout.bin`/`stdin.bin` 是二进制精确字节；不使用文本换行转换。`tc --test` 只读，侧车
  路径可跟随 `tests` 树内 symlink，但 fd 身份目标必须在所属测试目录内，且解析后必须是
  普通文件。侧车 `lstat` 返回 ENOENT/ENOTDIR → 取缺省值；返回其它错误 → 退出 70；
  条目存在时先按允许目标不存在的规则解析 symlink 链：若预期目标已在所属测试目录外，
  退出 2，优先于 dangling/不可用 70；目录内 `stat` 失败（dangling symlink、ELOOP）、
  解析后非普通文件（目录、FIFO、设备等）或打开/读取失败 → 退出 70。文件均以
  `O_NONBLOCK` 从 §6.1 已绑定测试目录 fd 打开并做 `fstat` 类型检查；允许的 symlink
  解析通过目录 fd 祖先身份或平台等价 beneath 机制验证不越界，不能校验后重新按裸路径
  open。校验后立即从该 fd 复制为私有只读快照（memfd 或工程外临时文件），再关闭原 fd；
  `stdin.bin` 的快照 fd 传给 worker，`stdout.bin` 由父进程从快照 fd 流式比较，`trap`/`exit`
  在同一快照 fd 上读取为有界快照。复制前后对原 fd 做 `fstat`，大小或宿主世代变化即退出
  70；不得校验后按名重开，避免同路径被替换为 FIFO 后阻塞，也避免校验后追加/改写改变
  实际输入或期望。格式错误同样退出 2。
- `trap`/`exit` 是小型文本侧车；单个文件读取上限 4 KiB，超过上限按格式错误退出 2。
  对这两个文件先以 `fstat` 大小或最多 4 KiB+1 的有界读取判定上限，再只把已判定的有界
  字节复制到私有快照，不得先无界复制巨大文件；`stdout.bin`/`stdin.bin` 不受此限，分别
  按流式比较与文件描述符快照处理。
- `trap`/`exit` 只去掉至多一个行尾 LF 或 CRLF；其余空白不 trim。`trap` 含任何空白、
  `exit` 含前后空白均按形式错误退出 2；保留的空白不得作为可比较的期望值。
- `trap` 必须是 ASCII 单行文本，形式为 `TC_RE_[A-Za-z0-9_]+`；去掉至多一个行尾后不得
  为空；拒绝 NUL/其它 Cc/Cf/Zl/Zp/空白；比较按字节长度而非 NUL 终止；格式错误退出 2；
  报告中的期望值只含 ASCII 字母、数字与下划线。
- `exit` 必须是 ASCII 无符号十进制 0..255；不接受符号、前后空格、多于一个行尾或其它字符。
  允许前导零，解析为数值后报告与比较统一使用无前导零的 `%u` 十进制形式。
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
- 测试入口必须能通过静态阶段；编译诊断按普通诊断格式输出，测试计为失败。根 `#lib`
  模式规则以 §1 为准，身份变化处理以 §6.1 为准；worker 复核后仍发现首个有效模式为
  `#lib` 时为 71。

### 6.3 执行

- 每个测试由一个私有 worker 执行：父进程 fork 后，worker 复用私有 core 的加载与
  `tc_vm_run`，不 exec `tc-vm`，不调用 AOT/clang/lld，不导出公共 API。无论是否设置
  超时都使用 worker；测试按名称顺序串行执行。worker 只继承白名单 fd：工程根、测试目录、
  main 私有快照、manifest include 目录、stdin 私有快照、stdout/stderr 捕获管道的正确端与
  运行所需内部 fd；其它调用者残留 fd 在 fork 前或 worker exec 边界前关闭，父进程也关闭
  不再使用的管道端。
- 父进程确定工程根、profile 与 include 顺序后传给 worker；worker 不重新从测试源路径
  做根发现。worker 通过 `fchdir` 已绑定工程根 fd 设置 cwd，并按 §6.1 的目录 fd 解析
  include/相对 import；stdin 始终来自 `stdin.bin`（缺省为空，立即 EOF），
  不继承调用者终端；stdout/stderr 由父进程捕获。stdout 与 `stdout.bin` 比较；stderr 不
  参与通过判定，仅作为失败详情输出。每个测试使用全新的 frontend/IR/CT/VM 状态。
- 父进程在 fork 前打开并校验当前测试的侧车，并保留由校验 fd 生成的私有快照；缺失取
  缺省值；canonical 目标在所属测试目录外或格式错误退出 2；目录内存在但不可用或读取失败
  退出 70；2/70 均停止后续测试并保留已输出报告。deadline 不覆盖父进程的侧车读取与结果
  比较。
- 默认不设 deadline；`tc --test --test-timeout=<seconds>` 可选设置单测试 deadline。
  `<seconds>` 只含 ASCII 十进制数字、非负、不接收符号/空格/小数，按 uint64 解析，超过
  `UINT64_MAX` 视为溢出并退出 2；缺省或 `0` 表示不限，正数从 worker 开始执行时计时，
  覆盖加载、CT/IR 与 VM 执行；deadline 换算饱和，不因乘法溢出提前到期。父进程用单调
  时钟计时；到期后向 worker 发送 SIGKILL 并回收，测试计为失败（退出 1），报告稳定的
  timeout 文本并继续后续测试。若 `waitpid` 已先返回 worker 的终止状态，则按该状态判定，
  不得因 deadline 随后的观察把正常退出改写成 timeout，也不得向已回收 pid 发信号。超时前
  stdout 继续排空并丢弃，不进入失败详情，也不作为通过依据。
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
- 正常执行完所有选中测试后打印 `<n> passed, <m> failed`；因用法、环境或内部错误提前终止时
  不打印汇总行，已输出的 ok/FAIL 行保留，终止原因写 stderr。0 个测试用 `0 tests` 代替汇总行。
- 退出码数值映射以工具链 §5.1 为唯一权威。stdout/trap/exit/编译诊断/timeout 失败计为
  测试失败并继续后续测试；用法、环境与内部错误停止后续测试，已输出的报告保留。
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
- 父进程全生命周期忽略或捕获 `SIGPIPE`。header、逐测试报告、汇总、失败详情与终止原因
  的每次 write、flush 及最终 close 失败都按宿主 IO 不可用退出 70：停止启动后续 worker，
  回收已在运行的 worker，关闭全部侧车/捕获 fd，不打印汇总，也不把写失败伪装成测试通过。
  stderr 本身不可写时仍执行清理并保持 70；不得为补报错误再次写坏通道。

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

## 8. 实现约束

- `tc` 链接私有 `tc_core`，与 `tc-vm` 共享私有 `run_source`；不新增公共 C 库或安装头文件。
  worker 只负责隔离、捕获与结果协议，父进程完成 manifest/路径解析、deadline、SIGKILL 与
  `waitpid` 回收，worker 不再做根发现，也不直接写用户报告。
- 捕获管道由父进程并发排空或落到工程外临时文件；所有退出路径统一回收 worker、关闭 fd、
  清理临时文件。worker 隔离依赖 POSIX fork/pipe。
- 结果协议只承载结构化测试结果，不包含 PID 或实际耗时；私有协议写入 `src/CONTRACTS.md`。
- 路径输出统一经 `tc_path_display`；部分检查模式同样确定工程根并使用 display，后端与 VM
  只消费共享 linemap，不自行重算。impl/IO 错误 detail 必须先 display 或去掉宿主绝对路径。
- `tc.project` 解析器不依赖外部 TOML/JSON 库。`src/project/`、`src/base/path.*`、
  `src/debug/linemap.*` 与驱动/前端的落点以实现文档为准。

## 9. 验收与测试

### 9.1 验收测试映射

下表是 0.0.47 交付时的测试注册映射；尚未落地的入口在实现并注册前不得作为当前仓库的
PASS 证据。

| 覆盖范围 | 测试入口 | 标签 |
| ---- | ---- | ---- |
| `tc` CLI、init、退出码与报告安全（§5、§7） | `tests/project/tc_cli_test.py` | `toolchain`, `fast` |
| manifest 语法、默认值、根发现与错误顺序（§3–§4） | `tests/project/manifest_test.py` | `toolchain`, `fast` |
| 测试发现、侧车、worker、超时、快照与报告（§6） | `tests/project/test_runner_test.py` | `toolchain`, `fast` |
| init 的 create/write/close/回滚故障注入（§5） | `tests/project/init_fault_test.py` | `toolchain`, `fast` |
| 根相对路径、模块身份、外部/回退与源文件边界（§4） | `tests/project/path_test.py` | `toolchain`, `fast` |
| AOT 源字符串与板端 trap 路径（§4） | `tests/project/path_aot_test.py` | `toolchain`, `aot` |
| monitor 的 module 与选择器（§4） | `tests/conformance/tools/`；客户端 `tests/tools/vm_gdb_client.py` | `toolchain`, `fast` |
| 跨根 asm/ELF/flat 确定性（§4.4、§9.2） | `tests/determinism/project_determinism.py` | `toolchain`, `slow` |
| 三入口契约与发布审计（§1、§9.2） | `tests/tools/s10_release_audit.py`、`tests/tools/cli_contract_test.py` | `toolchain` |
| harness 的 root 解析 | `tests/adapters/`、`tests/harness/` | `conformance` |

上表是覆盖入口，不重复规范条款；每个测试必须覆盖所引用章节的正例、反例与错误优先级。
新增测试沿用固定标签集合，`fast` 与 `slow` 互斥；外部依赖缺失按工具链文档 §7.1 的
既有策略处理，不得静默通过。

### 9.2 验收义务

- §3–§7 的每条规范都须由 §9.1 对应测试覆盖，包括正常路径、错误映射、双错/多错优先级，
  以及输出为空或部分提交时的可观察结果；测试描述不得取代或改写契约。
- init 端到端验收覆盖 §5 的完整生成、smoke、mkdir/create/write/close/回滚故障注入与
  报告失败；预期树差异、stdout/stderr 与退出码均以 §5 为准。
- 路径验收覆盖 §4 的跨 cwd 根发现与显示路径、外部/回退、无绝对路径泄漏和输出身份；
  涉及 CLI 相对 include 的用例须先固定等价的 canonical include 序列。
- 跨根 asm/ELF/flat 验收以[TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md) §5.8
  为准，本文件不重述确定性条件。
- `tc --test` 验收覆盖 §6 的 main/侧车私有快照、相对 import 的目录 fd 锚定、header 前
  联合身份检查、大输入/大输出、timeout 后继续、报告写失败与统一回收；快照闭包边界以
  §6.1/§10 为准。
- P4b/G11 的真实 gdb 测试入口、artifact 字段、签名与审计命令以[TC 开发调试工具链 0.0.47](TC开发调试工具链-0.0.47.md)
  §6.10/§7.2 为唯一权威；本地缺 gdb 的跳过不构成完成。
- 既有 `ctest`、golden、确定性、`docs.cleanup` 与发布审计零回归；三入口版本、release audit、
  AGENTS/README/docs map 与测试标签保持同步。

## 10. 限制

- init 假定 cwd 及新建路径在操作期间由调用者独占；不提供目录锁，也不承诺阻止身份复核后
  的并发替换。已有文件不修改，调用者须自行维护 `.gitignore` 与 `.gitattributes` 规则。
- `tc --test` 的私有快照与递归 import 边界以 §6.1 为准；worker 依赖 POSIX fork/pipe，
  其它宿主平台另案。
- 工程内运行时同样要求调用者保持工程独占；检查、打开与执行之间的竞态按 §4/§6 的身份
  复核和错误优先级处理，不以 canonical 字符串作为安全边界。
- 外部工具诊断与 0.0.46 路径/产物兼容边界以工具链 §5.8/§5.9 为准；TC 自身输出仍须
  满足 §4。
- gdb 的规范启动方式为从工程根启动；非工程根手动调试时设置 `directory` 仅作使用建议，
  不构成 P4b/G11 验收条件。真实 gdb 的完成证据按 §9.2 与工具链 §6.10。

## 11. 里程碑

| 阶段 | 内容 | 依赖 |
| ---- | ---- | ---- |
| **P0** 契约 | 三工具入口、manifest v1、测试侧车、worker/超时契约、路径契约；release audit/AGENTS/README 设计同步 | — |
| **P1** init/manifest | `tc --init-project`、`tc.project` 解析/根发现、模板、原子写入 | P0 |
| **P2** 路径模型 | display 字段、诊断/IR/trap/trace/linemap、external、跨根 | P1 |
| **P3** tc --test | 发现、侧车、worker 隔离/超时/捕获、私有 core 驱动、报告、过滤 | P2 |
| **P4a** AOT 路径/调试信息 | 单一共享行表生成器、`.asciz`、`.file`/`.loc`、DWARF v4 CU、comp_dir、跨根产物 | P2 |
| **P4b** 源级调试验收 | VM 调试 ELF 的验收与真实 gdb 的根相对源码行断点、顶层指令行覆盖 | P4a、T2、T3 |
| **P5** 验收 | 仓库测试、harness、release audit、文档/门禁 | P3、P4a、P4b |

与工具链 T1–T3 的路径消费者有重叠；P3 的 worker/超时与工具链 T5 测试基建对齐；P0 是前置。
P4a 必须先于 T1/T2 完成，T1/T2 再以其共享行表为前置；该顺序不构成 P4a 对 T1/T2 的依赖。
gdb 缺失时本地测试可显式跳过并记录原因，但 P4b 完成与 G11 PASS 必须提供工具链文档
§6.10 固定 Linux CI 作业产生的真实 gdb PASS artifact；跳过项不计为完成或 PASS。

## 12. 0.0.48 移交

- 0.0.47 只做单工程：`entry`、`tests/`、`profile`；多 target/workspace、依赖管理、lockfile
  不在本版本。
- 未来的 `tc --build/--run`、workspace、外部依赖解析以本文件的 manifest v1 为兼容基础。
- RTOS 相关的 board/device 配置不进 `tc.project`；保持工程文件只描述源码、测试与默认档案。

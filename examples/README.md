# 示例（examples）

教学向短程序，帮助快速上手 TC 0.0.42。**不是**符合性测试套件；回归用例仍在 `tests/`。

English: [README.en.md](README.en.md)

## 前提

先构建工具链：

```sh
make
```

下文假设当前目录为仓库根，`tc-vm` 位于 `./build/vm/bin/tc-vm`。

## 示例一览

| 文件 | 说明 | 期望 stdout（摘要） |
| ---- | ---- | ------------------- |
| [`hello.tc`](hello.tc) | 最小 `#program` | `42` |
| [`arith.tc`](arith.tc) | 显式类型算术与 `if` | `30` / `1` |
| [`control_flow.tc`](control_flow.tc) | `while` + `continue` / `break` | `1` `3` `4` |
| [`lib_and_import/`](lib_and_import/) | `#lib` + `import` + `funcall` | `7` |
| [`struct_basic.tc`](struct_basic.tc) | struct 构造与字段读写 | `10` `2` |
| [`ptr_basic.tc`](ptr_basic.tc) | `ptr` 取址 / 存 / 取 | `2` |

## 运行

```sh
./build/vm/bin/tc-vm examples/hello.tc
./build/vm/bin/tc-vm examples/arith.tc
./build/vm/bin/tc-vm examples/control_flow.tc
./build/vm/bin/tc-vm examples/struct_basic.tc
./build/vm/bin/tc-vm examples/ptr_basic.tc

# 模块：入口文件所在目录会自动加入搜索路径
./build/vm/bin/tc-vm examples/lib_and_import/main.tc
# 等价显式 -I：
./build/vm/bin/tc-vm -I examples/lib_and_import examples/lib_and_import/main.tc
```

仅静态检查：

```sh
./build/vm/bin/tc-vm --check examples/hello.tc
```

转译为 C99（写出同目录 `.c`；产物已在 `.gitignore`，勿提交）：

```sh
./build/aot/bin/tc-aot examples/hello.tc
```

## Embed

C 宿主嵌入示例请参阅根目录 [README.md](../README.md) 的「嵌入 TC-Embed」一节，以及 `tests/vm/embed/` 中的集成测试（非教学精简样例）。

## 下一步

- 语言权威：[docs/TC语言标准设计说明书-0.0.42.md](../docs/TC语言标准设计说明书-0.0.42.md)
- 文档地图：[docs/README.md](../docs/README.md)
- 贡献：[CONTRIBUTING.md](../CONTRIBUTING.md)

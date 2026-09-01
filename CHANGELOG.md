# 变更日志

[English](CHANGELOG.en.md)

本文件记录项目的所有重要变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号大致遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)（工具链 tag：`vMAJOR.MINOR.PATCH`）。

## [Unreleased]

### Added

- 开源仓库脚手架：贡献指南、行为准则、安全策略、Issue/PR 模板、示例与文档索引。
- 表面文档中英双语配对（文首可切换语言）。

## [0.0.42] - 2026-08-30

### Added

- 新增 `TC_CE_EXTRA_ARGUMENT` 错误码（N-13）。
- 确定性的自实现浮点十进制输出（FP-4.6）。

### Fixed

- 与端序无关的 `memblock` / `struct` 布局（FP-4.5）。
- 无 FENV 环境下可移植的下溢检测（不依赖 `__int128`，N-12）。
- N-12 剩余可移植性债务（指针偏移、AOT 分配、`.count`）。

### Changed

- 设计文档与实现版本升至 v0.0.42；0.0.42 遗留问题清零收口。

## [0.0.41] - 2026-08-30

### Added

- 导入的 struct 须使用限定名。
- struct `field_access` 作为操作数的全链路支持。

### Fixed

- 语言标准符合性缺口（P0–P6 收口）。
- 菱形 import / 拓扑、const 复合类型与 `memcopy` 下标缺口。
- const 堆释放路径与 const 宽度回调。
- 静态 `let` / `var` 初始化器中 `Self.field` 求值。
- `memblock_count` 重写后保留 struct `.count` 基址。

### Changed

- 设计文档以语言标准符合性为基线重编；版本升至 v0.0.41。

## [0.0.40] - 2026-08-24

### Changed

- 实现与设计文档升至 v0.0.40。
- Cursor Agent 文档重构与特性地图拆分。
- 为 `tc-0.0.40` 分支增加 CI/ASan 触发。

## [0.0.39] - 2026-08-22

### Added

- struct 自引用 / 指针类型与 struct 形式合规（0.0.39）。
- 会话级 `-I` 包含路径（补齐 0.0.39 剩余缺口）。

### Fixed

- `memcopy_unsafe` 在 `ptr<T>` 上操作时不带 memblock 头偏移。
- 文档刷新导致的块注释损坏。

### Changed

- 从 0.0.38 线带入跨平台 CI/CD 与测试移植，纳入 0.0.39 发布线。

[Unreleased]: https://github.com/tangrb/tc-compiler/compare/v0.0.42...HEAD
[0.0.42]: https://github.com/tangrb/tc-compiler/compare/v0.0.41...v0.0.42
[0.0.41]: https://github.com/tangrb/tc-compiler/compare/v0.0.40...v0.0.41
[0.0.40]: https://github.com/tangrb/tc-compiler/compare/v0.0.39...v0.0.40
[0.0.39]: https://github.com/tangrb/tc-compiler/compare/v0.0.38...v0.0.39

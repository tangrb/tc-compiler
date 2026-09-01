# 发版检查清单

[English](release-checklist.en.md)

打 `v*` tag 前请逐项确认。核心版本号与设计文档文件名须保持一致（当前 **0.0.42**）。

## 1. 代码与文档冻结

- [ ] 目标分支 CI 全绿（`ci.yml` + 视情况 `asan.yml`）
- [ ] 若版本升级：语言 / 编译器 / VM / AOT / Embed / libtc 设计书版本字符串已更新
- [ ] `tc-vm --version` / AOT 版本宏与目标 tag 一致
- [ ] 根目录 README 版本说明已更新（中 + 英）
- [ ] [CHANGELOG.md](../CHANGELOG.md) / [CHANGELOG.en.md](../CHANGELOG.en.md)：`[Unreleased]` 内容移入新 `## [X.Y.Z] - YYYY-MM-DD` 节并刷新 compare 链接

## 2. 本地验证

```sh
make ci
# 公开发 tag 前建议：
bash scripts/run_asan_all.sh
```

- [ ] `make ci` 通过
- [ ] ASan 矩阵通过（或记录明确延期原因）
- [ ] 发版所需改动均已提交（无遗漏未提交文件）

## 3. 打 tag 与发布

```sh
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

- [ ] 已推送附注 tag `vX.Y.Z`
- [ ] GitHub Actions `release.yml` 构建 Linux / macOS / Windows 产物
- [ ] GitHub Release 页面已创建（workflow 或手动），含 changelog 要点
- [ ] 验证下载二进制：`./tc-vm --version`

## 4. 发布后

- [ ] 在 `CHANGELOG.md` / `CHANGELOG.en.md` 重新打开 `[Unreleased]` 节
- [ ] 若旧版本不再支持，更新 `SECURITY.md` / `SECURITY.en.md` 受支持版本表
- [ ] 视需要创建 / 更新 `tc-0.0.xx` 维护分支与 CI 分支触发

## 备注

- Windows 发布构建使用 MSYS2 MinGW（非 MSVC）；AOT `--run` 需要 gcc 风格宿主 `cc`。
- 发布提交中**不要**包含 `Co-authored-by: Cursor <cursoragent@cursor.com>`。
- 安全修复：在 changelog 详述 exploit 前，遵循 [SECURITY.md](../SECURITY.md) 披露节奏。

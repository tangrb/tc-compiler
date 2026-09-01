# Release checklist

Use this list before cutting a `v*` tag. Core version and design-doc filenames must stay in sync (currently **0.0.42**).

## 1. Code and docs freeze

- [ ] Target branch green on CI (`ci.yml` + `asan.yml` as applicable)
- [ ] Language / compiler / VM / AOT / Embed / libtc design docs version strings updated if this is a version bump
- [ ] `tc-vm --version` / AOT version macros match the intended tag
- [ ] Root README version blurb updated (zh + en)
- [ ] [CHANGELOG.md](../CHANGELOG.md): move `[Unreleased]` notes into a new `## [X.Y.Z] - YYYY-MM-DD` section and refresh compare links

## 2. Local verification

```sh
make ci
# Optional but recommended before a public tag:
bash scripts/run_asan_all.sh
```

- [ ] `make ci` passes
- [ ] ASan matrix passes (or explicitly deferred with reason)
- [ ] No uncommitted changes required for the release (or they are committed)

## 3. Tag and publish

```sh
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

- [ ] Annotated tag `vX.Y.Z` pushed
- [ ] GitHub Actions `release.yml` builds Linux / macOS / Windows artifacts
- [ ] GitHub Release page created (workflow or manual) with changelog highlights
- [ ] Verify downloaded binaries: `./tc-vm --version`

## 4. After release

- [ ] Open `[Unreleased]` section again in `CHANGELOG.md` for ongoing work
- [ ] Update `SECURITY.md` supported-versions table if the previous release drops out of support
- [ ] Optionally create / update a `tc-0.0.xx` maintenance branch and CI branch filters

## Notes

- Windows release builds use MSYS2 MinGW (not MSVC); AOT `--run` needs a gcc-style host `cc`.
- Do not include `Co-authored-by: Cursor <cursoragent@cursor.com>` in release commits.
- Security fixes: follow [SECURITY.md](../SECURITY.md) disclosure timing before detailing exploits in the changelog.

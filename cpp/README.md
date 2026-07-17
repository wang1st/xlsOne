# xlsOne C++/Qt Port

This directory contains the cross-platform C++/Qt migration scaffold for xlsOne.

## Current Scope

- C++20 + Qt 6 + CMake project layout.
- `xlsone_core` static library with baseline data models, number parsing, `.xlsx` parsing, BIFF8 `.xls` parsing, merging, workbook validation, schema persistence, and template `.xlsx` export.
- `xlsone_app` Qt Widgets desktop shell with open/append/reload/clear/export actions, table rendering, drag/drop, source inspection, validation diagnostics, previous/next anomaly navigation, single-cell and bulk cell-type overrides, restore-automatic control, undo/clear correction controls, automatic adjustment memory, schema saving, and schema management.
- `xlsone_core_tests` Qt Test coverage for migrated baseline behavior plus real Xianju `.xlsx` and `.xls` parse/export regression checks.
- Swift/C++ golden snapshot tooling now emits stable JSON for parse/raw/format data, validation diagnostics, merged results, source drill-through, schema override application, schema matching, and export parse-back comparisons.
- Merge decisions now include the highest-impact Swift parity rules: header-row protection, zero-value sums, blank-as-zero numeric handling, code/label semantic vetoes, amount semantic boosts, neighbor-context hints, and common-prefix label display.
- Schema/rule support now includes workbook fingerprints with dominant-dimension voting, exact/ambiguous/similar matching, JSON persistence, automatic application of uniquely matched rules during Qt workspace recompute, automatic persistence of manual corrections, selected-cell restore/forget behavior, UI actions for saving manual corrections as reusable rules, plus a Qt dialog for viewing, applying, importing, exporting, and deleting saved schemas.
- Linux/UOS packaging metadata includes a desktop entry, spreadsheet MIME associations, and the reused app icon for `.deb` packaging.

## Build

```bash
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

Preset/script entry points:

```bash
cpp/scripts/build.sh dev
cpp/scripts/build.sh release
cpp/scripts/package_linux_deb.sh --bundle
cpp/scripts/package_linux_cpack_deb.sh uos-x86_64-release
```

Golden Swift/C++ parity check:

```bash
cpp/scripts/golden_compare.sh
cpp/scripts/golden_compare.sh /path/to/a.xlsx /path/to/b.xlsx
```

Snapshots are written to `tmp/golden/swift.json` and `tmp/golden/cpp.json` by default. Set `XLSONE_GOLDEN_OUT=/path/to/out` to change the output directory. The command exits non-zero when parity differences are found; that is expected while closing the migration gap.

Windows packaging entry point:

```powershell
cpp\scripts\package_windows_msi_zip.ps1 -Domestic
```

## Windows 授权与激活

Windows 版采用 Ed25519 签名授权文件 + 设备指纹绑定的付费授权模式：

- `LicenseManager`（`core/src/license_manager.cpp`）内置 Ed25519 公钥，可本地离线验证授权文件签名。
- Windows 设备指纹通过 PowerShell/WMIC 读取主板、CPU、硬盘序列号及 `MachineGuid`，并做 SHA-256 哈希。
- 在线激活调用 `POST /api/activate/windows`，返回签名授权 JSON。
- 离线激活在对话框中显示本机设备码，用户可在网页申请 `.license` 文件后导入。
- 设备组件哈希支持部分匹配，允许小范围硬件变动；大幅换机需走服务端迁移（每年 3 次）。

授权公钥与 Worker 私钥由 `activation/README.md` 管理；生产环境请重新生成密钥对。

UOS cross presets `uos-arm64-release` and `uos-loongarch64-release` expect `UOS_ARM64_TOOLCHAIN_FILE` and `UOS_LOONGARCH64_TOOLCHAIN_FILE` to point to prepared CMake toolchain files.

On macOS with Homebrew Qt, `qt-cmake` can also be used:

```bash
qt-cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

## Obfuscation / Release Hardening

The Qt build supports a lightweight, targeted obfuscation layer controlled by
the CMake option `XLSONE_OBFUSCATE`.

What it protects:
- Ed25519 license verification public key
- Activation base URL and API paths (`/api/trial/windows`, `/api/activate/windows`)
- Update base URL and download page path
- Hard-coded product URLs (`https://z-pulse.cn/xlsone/`, `https://xlsone.com/`)

What it does **not** do:
- Full control-flow flattening or VM protection (would break Qt MOC / signals / slots)
- Obfuscate third-party libraries (Qt, zlib, monocypher)
- Hide user-visible strings or persisted settings keys (preserves compatibility)

Enable it for a release build:

```bash
export XLSONE_LICENSE_PUBLIC_KEY="b0af99047cf30b4fe72360d53ff5765c3ccd33d911e2b1f19b99dcd4c8ff83cc"
cmake --preset windows-release
cmake --build --preset windows-release
```

When `XLSONE_OBFUSCATE=ON`:
- `scripts/generate_obfuscation.py` regenerates `core/src/obfuscated_secrets.cpp`
  in the build tree from the current public key and base URLs.
- Release-like builds also receive hardening flags (`-O3`, `-fvisibility=hidden`,
  `-fmerge-all-constants`, `-fno-ident`) and link-time stripping (`-s`).
- Windows packaging scripts run an additional `strip` step.

When obfuscation is disabled, the plaintext fallbacks compiled in are the
`kLicensePublicKey` array in `core/src/license_manager.cpp` and the
`XLSONE_*_BASE_URL` compile definitions — no generated secrets file is used.

## Known Gaps

- BIFF8 `.xls` parsing now covers OLE Compound File workbooks and common BIFF8 records used by the current fixtures: BoundSheet, SST/CONTINUE, FORMAT, XF, LABELSST, LABEL/RSTRING, NUMBER, RK/MULRK, BOOLERR, cached FORMULA results, and MERGEDCELLS expansion. It is not yet a full replacement for every legacy Excel edge case.
- The `.xlsx` parser covers workbook relationships, shared strings, styles, dates, numeric cells, inline strings, merged-cell expansion, and sparse row/column alignment. It does not yet cover every edge of the OOXML spec.
- `TemplateWorkbookExporter` now rewrites template `.xlsx` worksheet XML and repackages the workbook. CSV export remains as a fallback for non-`.xlsx` output paths.
- Smart merge parity is improved but not exhaustive; remaining work is mostly broader fixture coverage, platform packaging validation, and UI polish against the macOS workflow.
- DTK integration and packaging scripts are not added yet; the app uses plain Qt Widgets to keep Windows/Linux/UOS reuse straightforward.

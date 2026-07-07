# xlsOne 代码质量控制手册 (Code Quality Playbook)

> Owner: Senior Developer · Audience: 全体研发同学
> Goal: 让团队在不依赖某一个人"凭感觉"的情况下，也能稳定交付高质量代码。

---

## 0. 核心原则 (North Star)

1. **质量门禁分层**：本地 → 提交前钩子 → CI → 人工评审。越早发现的 bug 越便宜。
2. **无情自动化**：凡是机器能检查的（格式、静态分析、测试），不占用人工评审时间。
3. **模块边界不可破**：`xlsOneCore` 不依赖 `xlsOneUI`；`xlsOneUI` 不反向依赖业务具体实现。
4. **失败要响亮**：CI 红灯 = 合并冻结，没有人"先合后修"。
5. **可聚合性逻辑（merge engine）是皇冠上的明珠**：必须有测试覆盖，改动必须 PR + 双人评审。

---

## 1. 质量门禁分层架构

```
开发者本机                  提交前 (pre-commit)            CI (合并门禁)              人工评审
─────────────              ───────────────────           ───────────────           ──────────────
format on save     ──▶     swiftformat --lint             swift build                PR Template
swiftlint (实时)   ──▶     swiftlint --strict             swift test                 定义完成清单
IDE 警告 = 错误    ──▶     clang-format --check           swiftlint                   CODEOWNERS 必审
                             eslint --max-warnings 0       clang-tidy (C++)           资深 dev 签名
                             tsc --noEmit (TS)             coverage >= 阈值
```

---

## 2. 各语言落地清单

### Swift (核心 + UI)
| 工具 | 作用 | 配置位置 |
|------|------|----------|
| **SwiftFormat** | 自动格式化，消除风格争议 | `.swiftformat` |
| **SwiftLint** | 静态规则（过长函数、强制解包、TODO 标记） | `.swiftlint.yml` |
| **Build Settings** | `SWIFT_STRICT_CONCURRENCY=minimal`, 警告视为错误 | `Package.swift` / Xcode |
| **CI** | macOS runner 上 `swift build` + `swift test` + `swiftlint` | `.github/workflows/swift.yml` |

> 关键规则：禁用 `force_cast` / `force_try`；函数超过 60 行必须拆分；`// TODO:` 必须带 issue 编号。

### C++ / Qt
| 工具 | 作用 |
|------|------|
| `clang-format` | 格式统一（已部分有 CMake，补根配置） |
| `clang-tidy` | 静态分析（空指针、内存泄漏、modernize） |
| **补全测试矩阵** | 当前只在 Linux 跑 `ctest`，需加 Windows `ctest` |

### Node (`activation/worker`)
| 工具 | 作用 |
|------|------|
| **ESLint** (root 级) | 当前只有 node_modules 内的零散配置，需根级 `.eslintrc` |
| **Prettier** | 格式统一 |
| **tsc --noEmit** | 类型检查门禁 |
| **Vitest/Jest** | 激活逻辑单元测试（webhook 校验、授权码生成） |

### Web (`site/`)
- `stylelint` (CSS) + `htmlhint` (HTML) 轻量接入。

---

## 3. 人工评审规范 (Definition of Done)

每个 PR 合并前必须满足：

- [ ] CI 全绿（build + test + lint 无警告）
- [ ] 新增逻辑有对应单元测试，覆盖率不降
- [ ] 无 `// TODO` 无 issue 编号
- [ ] 模块依赖方向正确（见原则 3）
- [ ] UI 改动有截图 / 录屏（或注明 "无视觉变化"）
- [ ] 至少 1 名 CODEOWNERS 成员批准
- [ ] 可聚合核心逻辑变更需 2 人批准

提供 `docs/pull_request_template.md` 与根级 `CODEOWNERS`。

---

## 4. 团队能力提升路径 (Upskilling)

| 节奏 | 动作 | 产出 |
|------|------|------|
| 每周 | 公开代码评审 (live review)：资深 dev 带 1 个 PR 走查 | 团队统一品味 |
| 双周 | 技术 kata / 内部 talk（如 "如何安全地解析 Excel"） | 知识沉淀 |
| 持续 | 结对编程攻坚难点（解析器、schema 匹配） | 能力转移 |
| 每月 | 质量指标复盘：覆盖率 / 构建时长 / 红线次数 | 持续改进 |

**角色**：设 "质量负责人" 轮值，负责盯 CI 与门禁健康度，而非只靠资深一人。

---

## 5. 立即可落地的 4 个 wins

1. 加 `.swiftlint.yml` + `.swiftformat`，接 pre-commit。
2. 加 `.github/workflows/swift.yml`：macOS 上 build + test + lint。
3. 加根级 `.eslintrc` + `.prettierrc` + `tsc` 门禁（Node）。
4. 加 `pull_request_template.md` + `CODEOWNERS` + 本文档链接到 README。

> 下一步：与团队确认优先级后，由资深 dev 直接落地上述脚手架，并带第一轮 PR 走查。

# xlsOne 第一阶段发布流程

## 目标

这一阶段只解决“可归档、可上传”的工程阻断，不包含正式品牌素材和完整商店文案。

## 本地检查

1. 运行 `swift test`
2. 运行 `./scripts/validate_xcode_app.sh`
3. 确认 `App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset` 已生成图标
4. 如需在命令行检查归档结构，运行 `./scripts/archive_xcode_app.sh`

## 打开工程

1. 运行 `./scripts/generate_xcode_project.sh`
2. 用 Xcode 打开 `xlsOne.xcodeproj`
3. 选中 `xlsOneMacApp` target
4. 在 `Signing & Capabilities` 中确认：
   - Bundle Identifier 已替换成正式值
   - Team 已选择
   - App Sandbox 已启用
   - User Selected File Read/Write entitlement 已启用

## 归档与上传

1. 选择 `xlsOneMacApp` scheme
2. 使用 `Any Mac (Apple Silicon, Intel)` 或等效归档目的地
3. 执行 `Product > Archive`
4. 在 Organizer 中先做 `Validate App`
5. 验证无误后执行 `Distribute App > App Store Connect > Upload`

## 沙盒冒烟检查

在签名后的构建里至少手动验证一次：

- 导入文件
- 追加文件
- 拖拽导入
- 刷新
- 导出 `.xlsx`
- 调整后关闭重开，已记住调整仍能命中

## 当前占位项

- `project.yml` 里的 `PRODUCT_BUNDLE_IDENTIFIER` 仍是占位值
- 图标是临时提交版，正式品牌稿后替换
- 商店截图、隐私政策和支持 URL 在第二阶段补齐

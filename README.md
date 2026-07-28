# xlsOne - Excel 报表汇总工具

xlsOne 可把多个同模板 `.xlsx` / `.xls` 工作簿合并为一份汇总表。桌面版现已
迁移为纯 C11 实现，默认构建不使用也不链接 Qt，适合商业分发。

## 功能

- 拖放或选择多个 `.xlsx`、常见 BIFF8 `.xls` 文件
- 自动校验工作簿结构并切换多个工作表
- 区分金额、数量、编码和标签，按单元格智能汇总
- 点击汇总单元格查看每个来源文件的原始值
- 手动修正单元格为求和、标签或多值，并可恢复自动判断
- 导出 CSV，或基于原模板导出并保留表头、样式与行列结构的 `.xlsx`

## 纯 C 架构

```text
c/
├── include/xlsone/xlsone.h  公共 C API
├── src/                     XLSX/XLS、模型、校验、汇总、导出
├── app/                     SDL + Nuklear 桌面界面
└── tests/                   核心及真实样本回归测试
```

运行环境使用 SDL 2、Nuklear、zlib 和 Expat，均为允许商业使用的 C 组件。
详细许可证与分发义务见
[第三方组件声明](c/THIRD_PARTY_NOTICES.md)。

## 构建与运行

```sh
cmake -S . -B build -G Ninja -DXLSONE_C_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

macOS 应用位于 `build/c/app/xlsOne.app`；Linux/Windows 可执行文件位于
`build/c/app/`。各平台依赖安装和运行方式见[纯 C 版说明](c/README.md)。

也可运行完整验收：

```sh
make ci
```

生成商业分发友好的平台安装包：

```sh
make package
```

macOS 产物会把 SDL 放入应用包，并内置第三方许可证声明；正式对外发布前还需
使用开发者证书签名并完成 Apple 公证。

## 验证样本

`samples/monthly-report-sample-v1.1` 包含 4 个虚构部门月报和预期结果。
测试会验证两个工作表中的 36 个关键汇总值，并对导出的 XLSX 再次解析核对。

## 历史实现

- `cpp/`：原 Qt/C++ 客户端，仅作迁移参考，不进入默认构建或发布流程
- `Sources/`、`App/`：已有 macOS 原生实现，保留为独立代码线

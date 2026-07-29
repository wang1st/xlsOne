# xlsOne 纯 C 版

这是 xlsOne 桌面客户端的纯 C11 实现。它不包含 Qt 头文件、不运行 MOC/UIC，
生成的应用也不链接 Qt。

## 已实现

- `.xlsx`（OOXML）和常见 BIFF8 `.xls` 工作簿解析
- 多工作表结构校验和模板选择
- 金额、编码、标签、空值、重复计数和邻域语义的智能汇总
- 来源穿透、异常值统计和手动单元格类型修正
- 与 Qt 版一致的文件、编辑、修正规则、许可、语言和帮助菜单
- 英文、简体中文、繁体中文和日文界面，语言选择会在重启后保持
- 跨语言邻域语义词典；判断表格内容时不依赖当前界面语言
- 在线激活、14 天试用、离线许可证导入、设备绑定与导出水印限制
- 修正规则本地保存，并自动应用到结构一致的新工作簿
- CSV 导出，以及保留模板结构和样式的 `.xlsx` 导出
- SDL + Nuklear 桌面界面、拖放导入、文件选择、工作表切换和导出
- Windows、macOS、Linux 原生文件对话框适配

## 技术栈

- 核心与应用：ISO C11
- 桌面窗口、输入和渲染：SDL 2（zlib License）
- 控件：Nuklear 4.13.3（MIT / Public Domain）
- OOXML：项目内 ZIP 读写、zlib、Expat
- 授权：cJSON、Monocypher Ed25519

上述依赖均提供 C API 并允许商业分发。发布时须保留
[第三方组件声明](THIRD_PARTY_NOTICES.md)及相应许可证。

## 构建

macOS（Homebrew）：

```sh
brew install cmake ninja sdl2 expat zlib
cmake -S . -B build -G Ninja -DXLSONE_C_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
open build/c/app/xlsOne.app
```

Ubuntu / Debian：

```sh
sudo apt install cmake curl ninja-build libsdl2-dev libexpat1-dev zlib1g-dev
cmake -S . -B build -G Ninja -DXLSONE_C_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/c/app/xlsOne
```

Windows 可通过 vcpkg 安装 `sdl2`、`expat`、`zlib`，再把 vcpkg toolchain
传给 CMake。只构建核心库时可加 `-DXLSONE_C_BUILD_APP=OFF`。

分发版构建还需通过环境变量 `XLSONE_LICENSE_PUBLIC_KEY` 提供 64 位十六进制
Ed25519 公钥；私钥只保留在激活服务器上，不能进入客户端或安装包。

也可以直接在 `c/` 目录作为 CMake 源目录构建。Nuklear 固定为 4.13.3，
配置时会校验下载文件的 SHA-256。

生成可分发安装包：

```sh
cmake --build build --target package
```

自动发布会生成 macOS Universal DMG、Windows MSI/便携 ZIP，以及 Linux
amd64/arm64 DEB。macOS 包静态链接固定版本的 SDL，并内置许可证声明。
正式发布前仍需按平台完成可信代码签名和 Apple 公证。

## 验收

```sh
make ci
```

该命令使用严格编译告警、运行真实 `.xlsx` 解析/合并/导出回读测试，并检查
纯 C 源码和最终应用都没有 Qt 依赖。

旧 `cpp/` 目录只作为 Qt 迁移前的历史参考，不进入纯 C 构建和发布目标。

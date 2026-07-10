# Linux DEB 打包指引（ARM64 / AMD64）

**目标：** 在 Linux 机器上构建包含架构标识的 DEB 安装包，确保所有必要函数库都打包进去，安装后即可运行。

当前支持两种架构：

- `arm64` / `aarch64`：ARM64 处理器（如 Kylin、UOS ARM、树莓派、Apple Silicon Linux 等）
- `amd64` / `x86_64`：Intel / AMD 64 位处理器

---

## 1. 准备环境

```bash
# 系统要求：目标架构的 Ubuntu 20.04/22.04、Debian、UOS 或 Kylin

sudo apt update
sudo apt install -y build-essential cmake ninja-build dpkg-dev \
    qtbase5-dev libz-dev wget curl git
```

---

## 2. 拉取最新代码

```bash
cd ~
git clone https://gitee.com/wang1st/xlsone.git
cd xlsone
git checkout main
git pull origin main
```

> 注意：必须用最新 `main` 分支，因为已包含 ICU/libzstd 打包修复。

---

## 3. 下载 deepin Qt5.15 库

`build_deb.sh` 会根据当前机器架构自动下载对应的 deepin Qt5 包，也可以手动提前下载。

### 自动下载（推荐）

直接运行第 6 步的构建命令，脚本会自动下载。

### 手动下载

根据架构选择对应的包：

| 架构 | 文件名 | 下载 URL |
|---|---|---|
| ARM64 | `qt5.15-gles_231207_aarch64.tar.xz` | `https://github.com/deepin-community/sig-deepin-shared-libs/releases/download/Qt5.15.10-OpenGLES%3D%3D5.15.10%2Bszbt2/qt5.15-gles_231207_aarch64.tar.xz` |
| AMD64 | `qt5.15-gles_231205_amd64.tar.xz` | `https://github.com/deepin-community/sig-deepin-shared-libs/releases/download/Qt5.15.10-OpenGLES%3D%3D5.15.10%2Bszbt2/qt5.15-gles_231205_amd64.tar.xz` |

以 ARM64 为例：

```bash
cd ~/xlsone/cpp
mkdir -p deepin-qt5.15
cd deepin-qt5.15

wget "https://github.com/deepin-community/sig-deepin-shared-libs/releases/download/Qt5.15.10-OpenGLES%3D%3D5.15.10%2Bszbt2/qt5.15-gles_231207_aarch64.tar.xz"

tar -xf qt5.15-gles_231207_aarch64.tar.xz
```

解压后目录结构应为：

```text
~/xlsone/cpp/deepin-qt5.15/qt5.15-gles/
├── bin/
├── include/
├── lib/
└── plugins/
```

---

## 4. 检查需要打包的 ICU 版本

**这一步非常关键。** deepin Qt5 库依赖的 ICU 版本可能和系统 ICU 不同，必须下载匹配的 ICU 一起打包。

```bash
cd ~/xlsone/cpp/deepin-qt5.15/qt5.15-gles/lib
readelf -d libQt5Core.so.5.15.10 | grep libicu
```

输出示例：

```text
共享库：[libicui18n.so.63]
共享库：[libicuuc.so.63]
共享库：[libicudata.so.63]
```

如果显示 `so.63`，就下载 ICU 63；如果显示 `so.66`，就下载 ICU 66，以此类推。

> 经验：deepin-shared-libs Qt5.15 目前通常需要 **ICU 63**。

---

## 5. 下载对应 ICU 库

### ARM64（ICU 63）

```bash
cd ~/xlsone/cpp/deepin-qt5.15
mkdir -p icu63

# 从 Debian Buster archive 下载 ICU 63 arm64
cd /tmp
wget "http://archive.debian.org/debian/pool/main/i/icu/libicu63_63.1-6+deb10u3_arm64.deb"

dpkg-deb -x libicu63_63.1-6+deb10u3_arm64.deb icu63_extract

# 复制到项目目录
cp -r /tmp/icu63_extract/usr/* ~/xlsone/cpp/deepin-qt5.15/icu63/
```

### AMD64（ICU 63）

```bash
cd ~/xlsone/cpp/deepin-qt5.15
mkdir -p icu63

# 从 Debian Buster archive 下载 ICU 63 amd64
cd /tmp
wget "http://archive.debian.org/debian/pool/main/i/icu/libicu63_63.1-6+deb10u3_amd64.deb"

dpkg-deb -x libicu63_63.1-6+deb10u3_amd64.deb icu63_extract

# 复制到项目目录
cp -r /tmp/icu63_extract/usr/* ~/xlsone/cpp/deepin-qt5.15/icu63/
```

最终应存在：

```text
~/xlsone/cpp/deepin-qt5.15/icu63/usr/lib/<arch>-linux-gnu/
├── libicudata.so.63
├── libicui18n.so.63
└── libicuuc.so.63
```

其中 `<arch>-linux-gnu` 在 ARM64 上是 `aarch64-linux-gnu`，在 AMD64 上是 `x86_64-linux-gnu`。

> 如果第 4 步显示的是其他 ICU 版本（如 66、74），请去对应发行版仓库下载对应版本，并调整目录名（如 `icu66`、`icu74`）。

---

## 6. 构建安装包

```bash
cd ~/xlsone
./cpp/scripts/build_deb.sh --bundle
```

构建完成后输出：

```text
# ARM64
Package: ~/xlsone/cpp/build-linux-release/xlsone-1.0.4-linux-arm64.deb

# AMD64
Package: ~/xlsone/cpp/build-linux-release/xlsone-1.0.4-linux-amd64.deb
```

---

## 7. 验证打包的库

```bash
cd /tmp
rm -rf deb_check && mkdir deb_check
dpkg-deb -x ~/xlsone/cpp/build-linux-release/xlsone-1.0.4-linux-<arch>.deb deb_check

# 检查 Qt5 库
find deb_check/usr/lib/xlsone -name "libQt5*.so*" | sort

# 检查 ICU 版本是否匹配
find deb_check/usr/lib/xlsone -name "libicu*" | sort
readelf -d deb_check/usr/lib/xlsone/libQt5Core.so.5.15.10 | grep libicu

# 检查 libzstd
find deb_check/usr/lib/xlsone -name "libzstd*"
```

必须满足：

- `libQt5Core.so.5.15.10` NEEDED 中的 ICU 版本，与打包的 `libicui18n.so.X` 版本一致
- 包含 `libzstd.so.1`
- 包含 `libpcre2-16.so.0`
- 包含 `libdouble-conversion.so.3`

---

## 8. 在干净环境测试安装运行

```bash
# 找一台没有 Qt5 开发环境的同架构机器
sudo dpkg -i xlsone-1.0.4-linux-<arch>.deb
xlsone
```

如果能正常启动、不报错缺少 `.so`，则打包成功。

---

## 9. 上传

把生成的文件上传到网站 downloads 目录：

```bash
# ARM64
scp ~/xlsone/cpp/build-linux-release/xlsone-1.0.4-linux-arm64.deb \
  root@z-pulse.cn:/var/www/z-pulse.cn/downloads/xlsOne-1.0.2-linux-arm64.deb

# AMD64
scp ~/xlsone/cpp/build-linux-release/xlsone-1.0.4-linux-amd64.deb \
  root@z-pulse.cn:/var/www/z-pulse.cn/downloads/xlsOne-1.0.2-linux-amd64.deb
```

---

## 常见失败原因

| 报错 | 原因 | 解决 |
|---|---|---|
| `libicui18n.so.X not found` | 打包的 ICU 版本和 Qt5 需要的不一致 | 重新执行第 4-5 步，确保版本匹配 |
| `libzstd.so.1 not found` | 漏打包 libzstd | 确认系统有 libzstd，CMakeLists.txt 已自动打包 |
| `libpcre2-16.so.0 not found` | 漏打包 pcre2 | 确认系统有 libpcre2-16 |
| 启动后界面空白/崩溃 | Qt 插件路径不对 | 检查 deb 内 `/usr/lib/xlsone/plugins/platforms/libqxcb.so` 是否存在 |

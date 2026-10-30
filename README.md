# OmniCore-SDR-OnAir 3.3.0

**本项目是从 [adswill/OnAir](https://github.com/adswill/OnAir) fork 的独立项目，已接入 OmniCore SDR（AD9361）。** 桌面版通过 OmniCore Host 原生设备接口，支持接收（RX）、发射（TX）和全双工收发，并保留上游电视、广播和数字信号解码功能。Android 版支持 USB 直连接收，沿用上游的仅接收界面，暂不提供 TX。

This is an independent fork of OnAir with native OmniCore SDR integration, including RX, TX and full-duplex operation.

- [下载 Windows / Linux / Android 3.3.0](https://github.com/Liren66/OmniCore-SDR-OnAir/releases/tag/v3.3.0)
- [OmniCore Host 驱动库](https://github.com/Liren66/OmniCore-SDR-Host)
- [OmniCore 设备使用说明](DEVICES.md#omnicoresdr-omnicore-host)
- [上游完整说明与致谢](README.upstream.md)

## 本分支功能

- OmniCore Host C ABI 1 原生接入，支持接收、发射及全双工。
- RX1/RX2、TX1/TX2、采样率、增益和发射衰减设置。
- 无线电设置面板显示 RX RSSI；控制链路失效后重新打开设备。
- 更新检查指向本仓库，不会自动替换成上游发行包。

版本号跟随上游，当前统一为 **3.3.0**，不再添加 `-omnicore.*` 或 Android 后缀。程序名称保留 **OmniCore-SDR-OnAir**，以区分本分支和上游。2026-10-30 再次拉取上游，`main` 仍为已合并的 `8ee15736`；保留上游 Git 历史和本地 OmniCore 功能提交。

| 发布包 | 平台 | OmniCore 功能 |
| --- | --- | --- |
| `OmniCore-SDR-OnAir-3.3.0-windows-x64.zip` | Windows x64 | RX / TX / 全双工 |
| `OmniCore-SDR-OnAir-3.3.0-linux-x86_64.tar.gz` | Ubuntu 24.04 x86-64 | RX / TX / 全双工 |
| `OmniCore-SDR-OnAir-3.3.0-android-arm64-v8a.apk` | Android 9+ ARM64，需 USB Host/OTG | USB RX |

发布附件仅包含这三个程序包及 `SHA256SUMS` 校验和，不单独上传日志、测试报告或源码压缩包；源码可从本仓库的版本标签获取。macOS 暂不发布。

## Windows 使用

解压整个 ZIP，运行 `OmniCore-SDR-OnAir.exe`。包内包含 OmniCore Host `omnicore.dll` 和程序运行依赖。USB 设备仍需正确的系统驱动；连接方法、网络地址及参数见 [DEVICES.md](DEVICES.md)。

命令行工具为 `onair-cli.exe`、`onair-scan.exe`；`omnicore_probe.exe` 用于手动硬件诊断。探测工具可能接入真实设备，请先阅读 `--help`。程序沿用上游 OnAir 的配置目录，以兼容现有设置。

Windows 发布包使用静态 LGPL FFmpeg，不包含系统 FFmpeg 的摄像头采集组件，也不捆绑可选的 Hamlib、SoapySDR、RFNM。未连接硬件时的测试不能代替真实射频验收。

## 从源码构建与打包

在 MSYS2 UCRT64 环境安装 GCC、CMake、Ninja、pkgconf、Python、Rust、GLFW、libhackrf、libusb、make、nasm。首次准备 FFmpeg 8.0 源码归档：

```sh
mkdir -p build-release/deps
curl -fL https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz -o build-release/deps/ffmpeg-8.0.tar.xz
bash tools/package/build_ffmpeg_static.sh "$PWD/build-release/deps/ffmpeg" "$PWD/build-release/deps/ffmpeg-8.0.tar.xz"
bash tools/package/build_omnicore_windows.sh
cmake -S ../OmniCore-SDR-Host -B build-release/omnicore-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release/omnicore-host --target omnicore --parallel 4
python tools/package/package_omnicore_windows.py --host-build build-release/omnicore-host --host-source ../OmniCore-SDR-Host
```

打包脚本使用真实 OmniCore Host 库，拒绝测试替身；依赖及导入符号检查成功后才生成 ZIP。构建缓存统一放在 `build-release/`，发布产物放在 `release/`，均不提交 Git。`voice_listen/` 是上游测试样本，不是临时文件。

## Linux 构建

Ubuntu 24.04 x86-64 用户可以直接解压 Linux 发布包，并在图形桌面中运行根目录的 `./OmniCore-SDR-OnAir`。运行 `./install-desktop.sh` 可将带图标的快捷方式加入当前用户的应用菜单；移动程序目录后请重新运行此脚本。启动脚本会加载包内的 OmniCore Host 库；系统依赖和 USB 权限配置见包内 `RUN-LINUX.txt`。使用普通用户运行即可，无需 `sudo`。

Ubuntu 24.04 x86-64 依赖：`build-essential cmake ninja-build pkg-config cargo-1.89 rustc-1.89 nasm libhackrf-dev libusb-1.0-0-dev libglfw3-dev libasound2-dev`。推荐 Rust 1.89 或更新版；Ubuntu 默认的 1.75 无法编译当前上游。按上面的步骤准备静态 FFmpeg，然后运行 `CARGO=/usr/bin/cargo-1.89 RUSTC=/usr/bin/rustc-1.89 bash tools/package/build_omnicore_linux.sh`。生成的 GUI 为 `build-release/dect2`。

OmniCore Host 可以另行安装，或在同一构建树中从它的源码构建 `libomnicore.so`；运行程序时将其目录加入 `LD_LIBRARY_PATH`。USB 权限规则见 `packaging/linux/60-onair-sdr.rules`。

真实硬件诊断需要先关闭其他占用设备的程序。多个设备连接时必须传 `--serial`。`omnicore_probe --help` 本身不会打开设备；`--tx` 会实际发射，其单音位于 `--txfreq` 之上 100 kHz。使用 `--tx-retune-step 0` 可在后续衰减调整测试中保持发射频率不变。

排查启动溢出时，可以增加 `--steady-secs 30`：在首次接收后保持同一个 RX 流连续运行，并逐秒输出接收结果和累计溢出计数，便于区分启动阶段与稳定运行阶段的故障。该检查本身不会打开 TX。

## Android 使用

安装 APK 后，通过 USB Host/OTG 连接 OmniCore SDR，允许系统 USB 授权，在 **Radio** 页面选择 **OmniCore SDR** 并启动接收。建议先使用 4 MSPS；需要时使用带电源的 USB 集线器。无需 root，也无需安装 WinUSB、libusbK 或额外 USB 驱动应用。

已修复早期 APK 的驱动能力登记和 USB 设备列表未接入主界面问题，用户已在 Android 17 设备上确认修复版可以使用。持续吞吐性能取决于手机、USB 接口和供电；本次版本统一不额外进行射频测试。

3.3.0 同时修正 Table 页无数据、搜索无结果时的提前退出方式，解决用户日志中的界面闪退，并修正消息、飞行器、船舶及 RDS 页的相同写法。

对外版本为 `3.3.0`；Android 内部安装版本号为 `3030099`，高于早期 APK 的 `3030001` / `3030002`。应用包名和签名保持一致，可以直接覆盖安装并保留设置。环境配置、构建步骤和签名说明见 [Android README](android/README.md)。

## 许可证与来源

代码按 [GPL-3.0-or-later](LICENSE) 发布。上游作者为 Kristijonas Adomaitis 与 OnAir contributors，第三方依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。本项目是独立分支，不代表上游背书。程序沿用上游 OnAir 图标；图标的独立版权声明保留在 THIRD_PARTY.md 中，不属于代码的 GPL 授权范围。源码内保留上游版权和历史记录。

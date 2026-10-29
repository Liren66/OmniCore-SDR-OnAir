# OmniCore-SDR-OnAir

面向 OmniCore-SDR / AD9361 的 OnAir 分支，基于 [adswill/OnAir](https://github.com/adswill/OnAir)，保留上游电视、广播和数字信号解码功能，并集成 OmniCore Host 原生设备接口。

- [下载 Windows x64 便携包](https://github.com/Liren66/OmniCore-SDR-OnAir/releases/latest)
- [OmniCore Host 驱动库](https://github.com/Liren66/OmniCore-SDR-Host)
- [OmniCore 设备使用说明](DEVICES.md#omnicoresdr-omnicore-host)
- [上游完整说明与致谢](README.upstream.md)

## 本分支功能

- OmniCore Host C ABI 1 原生接入，支持接收、发射及全双工。
- RX1/RX2、TX1/TX2、采样率、增益和发射衰减设置。
- 无线电设置面板显示 RX RSSI；控制链路失效后重新打开设备。
- 更新检查指向本仓库，不会自动替换成上游发行包。

首次版本 `3.3.0-omnicore.1` 合并上游 `main` 提交 `8ee15736`（2026-10-29 拉取）。保留上游 Git 历史和本地 OmniCore 功能提交。

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

Ubuntu 24.04 x86-64 依赖：`build-essential cmake ninja-build pkg-config cargo-1.89 rustc-1.89 nasm libhackrf-dev libusb-1.0-0-dev libglfw3-dev libasound2-dev`。推荐 Rust 1.89 或更新版；Ubuntu 默认的 1.75 无法编译当前上游。按上面的步骤准备静态 FFmpeg，然后运行 `CARGO=/usr/bin/cargo-1.89 RUSTC=/usr/bin/rustc-1.89 bash tools/package/build_omnicore_linux.sh`。生成的 GUI 为 `build-release/dect2`。

OmniCore Host 可以另行安装，或在同一构建树中从它的源码构建 `libomnicore.so`；运行程序时将其目录加入 `LD_LIBRARY_PATH`。USB 权限规则见 `packaging/linux/60-onair-sdr.rules`。

真实硬件诊断需要先关闭其他占用设备的程序。多个设备连接时必须传 `--serial`。`omnicore_probe --help` 本身不会打开设备；`--tx` 会实际发射，其单音位于 `--txfreq` 之上 100 kHz。使用 `--tx-retune-step 0` 可在后续衰减调整测试中保持发射频率不变。

## 许可证与来源

代码按 [GPL-3.0-or-later](LICENSE) 发布。上游作者为 Kristijonas Adomaitis 与 OnAir contributors，第三方依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。本项目是独立分支，不代表上游背书；Windows 修改版不嵌入上游受限图标。源码内保留上游版权和历史记录。

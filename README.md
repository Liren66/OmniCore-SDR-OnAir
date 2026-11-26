# OmniCore-SDR-OnAir 4.0.0

**English** · [简体中文](README.zh-CN.md)

**This is an independent project forked from [adswill/OnAir](https://github.com/adswill/OnAir), with OmniCore SDR (AD9361) support added.** The desktop version uses the native OmniCore Host device interface for receive (RX), transmit (TX), and full duplex, and keeps upstream's TV, broadcast, and digital signal decoding. The Android version supports direct USB receive, keeps upstream's receive-only interface, and does not offer TX yet.

- [Download builds for all four platforms](https://github.com/Liren66/OmniCore-SDR-OnAir/actions/workflows/omnicore-build.yml)
- [OmniCore Host driver library](https://github.com/Liren66/OmniCore-SDR-Host)
- [Using the OmniCore device](DEVICES.md#omnicoresdr-omnicore-host)
- [Full upstream README and acknowledgements](README.upstream.md)

## What this fork adds

- Native OmniCore Host C ABI 1 support, with receive, transmit, and full duplex.
- RX1/RX2, TX1/TX2, sample rate, gain, and TX attenuation settings.
- RX RSSI shown in the radio settings panel; the device is reopened after the control link fails.
- Update checks point to this repository and never replace it with an upstream release.

The version number follows upstream, currently **4.0.0** everywhere, with no `-omnicore.*` or Android suffix. The program name stays **OmniCore-SDR-OnAir** to tell this fork apart from upstream. This release merges upstream `a63c3a8eb` and bundles OmniCore Host **0.8.0** on all four platforms; the upstream Git history and the local OmniCore feature commits are both kept.

> **Firmware requirement**: the bundled OmniCore Host **0.8.0** works with firmware [v2026.11.08](https://github.com/Liren66/OmniCore-SDR-Firmware/releases/tag/v2026.11.08) (and any framed-USB firmware since v2026.11.07). Firmware older than v2026.11.07 must be updated first; older builds of this program (bundling OCH 0.5.0) cannot open the new firmware. When the board runs [OmniCore SDR Linux](https://github.com/Liren66/OmniCore-SDR-Linux) 1.3.x, the firmware is already in the image.

| Package | Platform | OmniCore features |
| --- | --- | --- |
| `OmniCore-SDR-OnAir-4.0.0-windows-x64.zip` | Windows x64 | RX / TX / full duplex |
| `OmniCore-SDR-OnAir-4.0.0-windows-x64-setup.exe` | Windows x64 installer | RX / TX / full duplex |
| `OmniCore-SDR-OnAir-4.0.0-linux-x86_64.tar.gz` | Ubuntu 24.04 x86-64 | RX / TX / full duplex |
| `OmniCore-SDR-OnAir-4.0.0-macos-arm64.dmg` | macOS 15+, Apple Silicon | RX / TX / full duplex |
| `OmniCore-SDR-OnAir-4.0.0-android-arm64-v8a.apk` | Android 9+ ARM64, needs USB Host/OTG | USB RX |

In the OmniCore builds workflow on GitHub Actions, pick a platform, or pick `all` to build all four. When the run finishes, download from Artifacts on the run page. The Android workflow can produce an unsigned APK; to install over a published app you need to sign it with the original release key.

## Windows

Unzip the whole ZIP and run `OmniCore-SDR-OnAir.exe`. The package includes the OmniCore Host `omnicore.dll` and the program's runtime dependencies. USB devices still need the right system driver; connection steps, network addresses, and parameters are in [DEVICES.md](DEVICES.md).

The command-line tools are `onair-cli.exe` and `onair-scan.exe`; `omnicore_probe.exe` is for manual hardware diagnostics. The probe tool may access real hardware, so read `--help` first. The program uses upstream OnAir's configuration folder so existing settings keep working.

The Windows package uses a static LGPL FFmpeg without the system FFmpeg's camera capture components, and does not bundle the optional Hamlib, SoapySDR, or RFNM. Tests without hardware attached do not replace real RF acceptance testing.

## Building and packaging from source

In an MSYS2 UCRT64 environment, install GCC, CMake, Ninja, pkgconf, Python, Rust, GLFW, libhackrf, libusb, make, and nasm. The first time, fetch the FFmpeg 8.0 source archive:

```sh
mkdir -p build-release/deps
curl -fL https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz -o build-release/deps/ffmpeg-8.0.tar.xz
bash tools/package/build_ffmpeg_static.sh "$PWD/build-release/deps/ffmpeg" "$PWD/build-release/deps/ffmpeg-8.0.tar.xz"
bash tools/package/build_omnicore_windows.sh
cmake -S ../OmniCore-SDR-Host -B build-release/omnicore-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release/omnicore-host --target omnicore --parallel 4
python tools/package/package_omnicore_windows.py --host-build build-release/omnicore-host --host-source ../OmniCore-SDR-Host
```

The packaging script uses the real OmniCore Host library and refuses test doubles; it produces the ZIP only after the dependency and imported-symbol checks pass. Build caches go in `build-release/` and release outputs in `release/`; neither is committed to Git. `voice_listen/` holds upstream test samples, not temporary files.

## Linux

On Ubuntu 24.04 x86-64 you can unpack the Linux package and run `./OmniCore-SDR-OnAir` from its root folder in a graphical desktop. Run `./install-desktop.sh` to add a shortcut with an icon to your application menu; run it again if you move the program folder. The launcher script loads the bundled OmniCore Host library; system dependencies and USB permissions are covered in `RUN-LINUX.txt` in the package. Run it as a normal user; no `sudo` needed.

Ubuntu 24.04 x86-64 build dependencies: `build-essential cmake ninja-build pkg-config cargo-1.89 rustc-1.89 nasm libhackrf-dev libusb-1.0-0-dev libglfw3-dev libasound2-dev`. Rust 1.89 or newer is recommended; Ubuntu's default 1.75 cannot build the current upstream. Prepare the static FFmpeg as above, then run `CARGO=/usr/bin/cargo-1.89 RUSTC=/usr/bin/rustc-1.89 bash tools/package/build_omnicore_linux.sh`. The resulting GUI is `build-release/dect2`.

OmniCore Host can be installed separately, or `libomnicore.so` can be built from its source in the same build tree; add its folder to `LD_LIBRARY_PATH` when running the program. USB permission rules are in `packaging/linux/60-onair-sdr.rules`.

For real hardware diagnostics, close any other program using the device first. With several devices connected you must pass `--serial`. `omnicore_probe --help` does not open the device; `--tx` really transmits, with a single tone 100 kHz above `--txfreq`. Use `--tx-retune-step 0` to keep the TX frequency fixed during the attenuation-step tests that follow.

To investigate startup overflows, add `--steady-secs 30`: after the first receive, it keeps the same RX stream running and prints the receive result and cumulative overflow count every second, which separates startup failures from steady-state ones. This check never turns on TX.

## Android

After installing the APK, connect OmniCore SDR over USB Host/OTG, allow the system USB permission prompt, choose **OmniCore SDR** on the **Radio** page, and start receiving. Start with 4 MSPS; use a powered USB hub if needed. No root, WinUSB, libusbK, or extra USB driver app is required.

Earlier APKs failed to register the driver capability and did not connect the USB device list to the main screen; both are fixed, and a user has confirmed the fixed build works on an Android 17 device. Sustained throughput depends on the phone, the USB port, and power; this release has no additional RF testing.

The fixes carried over from 3.3.0 correct how the Table page exits early when there is no data or a search has no results, which resolves the UI crash in a user's log, and apply the same fix to the Messages, Aircraft, Vessels, and RDS pages.

The public version is `4.0.0`; the Android internal version code is `4000099`, higher than the earlier APKs' `3030001` / `3030002`. The package name and signature are unchanged, so you can install over the old app and keep your settings. Environment setup, build steps, and signing are in the [Android README](android/README.md).

## License and origin

The code is released under [GPL-3.0-or-later](LICENSE). The upstream authors are Kristijonas Adomaitis and the OnAir contributors; third-party dependencies are listed in [THIRD_PARTY.md](THIRD_PARTY.md). This project is an independent fork and is not endorsed by upstream. The program uses upstream OnAir's icon; the icon's separate copyright notice is kept in THIRD_PARTY.md, and the icon is not covered by the code's GPL license. Upstream copyright notices and history are kept in the source.

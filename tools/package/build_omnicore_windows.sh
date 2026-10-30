#!/usr/bin/env bash
# Native UCRT64 release build. Run from an MSYS2 UCRT64 shell.
set -euo pipefail
cd "$(dirname "$0")/../.."
[[ ${MSYSTEM:-} == UCRT64 ]] || { echo 'Use an MSYS2 UCRT64 shell.' >&2; exit 1; }
build=build-release
version=$(grep -m1 -oE 'project\(OnAir VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Cannot read upstream version' >&2; exit 1; }
prefix="$PWD/$build/deps/ffmpeg"
[[ -f "$prefix/lib/libavcodec.a" ]] || { echo 'Build the LGPL FFmpeg first (README.md).' >&2; exit 1; }
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDECT2_BUILD_APP=ON -DDECT2_BUILD_TESTS=ON \
  -DDECT2_FFMPEG_PREFIX="$(cygpath -m "$prefix")" \
  -DDECT2_HAVE_HAMLIB=OFF -DDECT2_WITH_SOAPY=OFF -DDECT2_WITH_RFNM=OFF \
  -DONAIR_VERSION="$version"
cmake --build "$build" --parallel "${BUILD_JOBS:-8}" --target \
  dect2 dect2cli dect2scan onair-server onair-plugin-host omnicore_probe \
  test_omnicore test_backup test_latest_slot test_gui_rules test_updater test_updater_mirror
# Windows searches System32 before PATH; an old global libusb can otherwise shadow UCRT64.
for dll in libusb-1.0.dll libhackrf.dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libiconv-2.dll libatomic-1.dll glfw3.dll; do
  [[ ! -f "$MSYSTEM_PREFIX/bin/$dll" ]] || cp "$MSYSTEM_PREFIX/bin/$dll" "$build/"
done
ctest --test-dir "$build" --output-on-failure -R '^(omnicore|backup|latest_slot|gui_rules|updater|updater_mirror)$'

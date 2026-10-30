#!/usr/bin/env bash
# Native Linux build; Ubuntu 24.04 x86-64 is the first verified target.
set -euo pipefail
cd "$(dirname "$0")/../.."
build=build-release
version=$(grep -m1 -oE 'project\(OnAir VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Cannot read upstream version' >&2; exit 1; }
prefix="$PWD/$build/deps/ffmpeg"
[[ -f "$prefix/lib/libavcodec.a" ]] || { echo 'Build the LGPL FFmpeg first (README.md).' >&2; exit 1; }
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDECT2_BUILD_APP=ON -DDECT2_BUILD_TESTS=ON \
  -DDECT2_FFMPEG_PREFIX="$prefix" \
  -DONAIR_CARGO="${CARGO:-$(command -v cargo)}" -DONAIR_RUSTC="${RUSTC:-$(command -v rustc)}" \
  -DDECT2_HAVE_HAMLIB=OFF -DDECT2_WITH_SOAPY=OFF -DDECT2_WITH_RFNM=OFF \
  -DONAIR_VERSION="$version"
cmake --build "$build" --parallel "${BUILD_JOBS:-6}" --target \
  dect2 dect2cli dect2scan onair-server onair-plugin-host omnicore_probe \
  test_omnicore test_backup test_latest_slot test_gui_rules test_updater test_updater_mirror
ctest --test-dir "$build" --output-on-failure -R '^(omnicore|backup|latest_slot|gui_rules|updater|updater_mirror)$'

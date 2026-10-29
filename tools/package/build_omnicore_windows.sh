#!/usr/bin/env bash
# Native UCRT64 release build. Run from an MSYS2 UCRT64 shell.
set -euo pipefail
cd "$(dirname "$0")/../.."
[[ ${MSYSTEM:-} == UCRT64 ]] || { echo 'Use an MSYS2 UCRT64 shell.' >&2; exit 1; }
build=build-release
prefix="$PWD/$build/deps/ffmpeg"
[[ -f "$prefix/lib/libavcodec.a" ]] || { echo 'Build the LGPL FFmpeg first (README.md).' >&2; exit 1; }
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDECT2_BUILD_APP=ON -DDECT2_BUILD_TESTS=ON \
  -DDECT2_FFMPEG_PREFIX="$(cygpath -m "$prefix")" \
  -DDECT2_HAVE_HAMLIB=OFF -DDECT2_WITH_SOAPY=OFF -DDECT2_WITH_RFNM=OFF \
  -DONAIR_VERSION=3.3.0-omnicore.1
cmake --build "$build" --parallel "${BUILD_JOBS:-8}" --target \
  dect2 dect2cli dect2scan onair-server onair-plugin-host omnicore_probe \
  test_omnicore test_backup test_latest_slot test_gui_rules test_updater test_updater_mirror
ctest --test-dir "$build" --output-on-failure -R '^(omnicore|backup|latest_slot|gui_rules|updater|updater_mirror)$'

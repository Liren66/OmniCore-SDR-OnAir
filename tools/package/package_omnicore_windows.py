#!/usr/bin/env python3
"""Package a verified UCRT64 build without copying build logs or test DLLs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = '3.3.0-omnicore.1'


def run(*args, cwd=ROOT):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True, errors='replace')


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-build', type=Path, required=True)
    parser.add_argument('--host-source', type=Path, required=True)
    args = parser.parse_args()
    build = ROOT / 'build-release'
    host = args.host_build.resolve()
    source = args.host_source.resolve()
    if not (source / 'include').is_dir() or not (host / 'CMakeCache.txt').is_file():
        raise SystemExit('A real OmniCore Host source and build tree are required.')
    host_dll = host / 'omnicore.dll'
    exports = run('objdump', '-p', host_dll)
    if 'fake_get' in exports or 'och_test_' in exports or 'och_open' not in exports:
        raise SystemExit('The OmniCore library is not a production Host library.')
    release = ROOT / 'release'
    release.mkdir(exist_ok=True)
    out = release / f'OmniCore-SDR-OnAir-{VERSION}-windows-x64'
    out.mkdir()  # Never overwrite a previously packaged release.
    binaries = {
        'dect2.exe': 'OmniCore-SDR-OnAir.exe',
        'dect2cli.exe': 'onair-cli.exe',
        'dect2scan.exe': 'onair-scan.exe',
        'onair-server.exe': 'onair-server.exe',
        'onair-plugin-host.exe': 'onair-plugin-host.exe',
        'omnicore_probe.exe': 'omnicore_probe.exe',
    }
    for src, dst in binaries.items():
        subprocess.check_call(['strip', '-o', str(out / dst), str(build / src)])
    subprocess.check_call(['strip', '-o', str(out / 'omnicore.dll'), str(host_dll)])
    # OmniCore Host loads this transport at run time, so PE imports do not find it.
    shutil.copy2(host / 'libusbK.dll', out / 'libusbK.dll')

    ucrt = Path(shutil.which('gcc')).resolve().parent
    search = [ucrt, host, host / 'stage/bin', build / 'third_party/whisper/src',
              build / 'third_party/whisper/ggml/src', build / 'third_party/whisper/ggml/src/ggml-cpu']
    # Only real optional libraries from the toolchain; never build/omnicore_fake.
    for name in ['libcodec2.dll', 'librtlsdr.dll', 'libairspy.dll', 'libairspyhf.dll',
                 'libbladeRF.dll', 'libLimeSuite.dll', 'libiio.dll']:
        choices = [build / name, ucrt / name] if name == 'libcodec2.dll' else [ucrt / name]
        for candidate in choices:
            if candidate.is_file():
                shutil.copy2(candidate, out / name)
                break
    queue = list(out.glob('*.exe')) + list(out.glob('*.dll'))
    seen = set()
    provenance = {}
    system = Path(os.environ['SystemRoot']) / 'System32'
    while queue:
        path = queue.pop()
        if path.name.lower() in seen:
            continue
        seen.add(path.name.lower())
        for dll in re.findall(r'DLL Name:\s*(\S+)', run('objdump', '-p', path)):
            if (out / dll).is_file():
                continue
            candidate = next((p / dll for p in search if (p / dll).is_file()), None)
            if candidate:
                shutil.copy2(candidate, out / dll)
                provenance[dll] = candidate.name
                queue.append(out / dll)
            elif dll.lower().startswith(('api-ms-win-', 'ext-ms-win-')) or (system / dll).is_file():
                continue
            else:
                raise SystemExit(f'Missing dependency: {path.name} -> {dll}')

    for filename in ['LICENSE', 'THIRD_PARTY.md', 'README.md', 'README.upstream.md', 'DEVICES.md']:
        shutil.copy2(ROOT / filename, out / filename)
    shutil.copytree(ROOT / 'web/onair-web', out / 'web/onair-web')
    shutil.copy2(ROOT / 'LICENSE', out / 'LICENSE.txt')
    shutil.copytree(ROOT / 'LICENSES', out / 'LICENSES')
    licenses = out / 'licenses'
    licenses.mkdir()
    shutil.copy2(source / 'LICENSE', licenses / 'OmniCore-Host-LICENSE.txt')
    shutil.copytree(source / 'third_party/driver/libusbK-3.1.0.0-src/license', licenses / 'libusbK')
    shutil.copy2(build / 'deps/ffmpeg/share/ffmpeg-configuration.txt', licenses / 'ffmpeg-configuration.txt')
    shutil.copy2(ROOT / 'tools/package/build_ffmpeg_static.sh', licenses / 'build_ffmpeg_static.sh')
    msys_licenses = ucrt.parent / 'share/licenses'
    # Carry the installed dependency notices, including GCC runtime exceptions.
    if msys_licenses.is_dir():
        shutil.copytree(msys_licenses, licenses / 'msys2')
    (out / 'LICENSE-NOTICES-BINARIES.md').write_text(
        '# Binary source and licence notices\n\n'
        'OmniCore-SDR-OnAir: GPL-3.0-or-later; https://github.com/Liren66/OmniCore-SDR-OnAir.\n'
        'Based on OnAir by Kristijonas Adomaitis and contributors. This modified Windows package does not embed the upstream logo.\n\n'
        'FFmpeg 8.0: LGPL-2.1-or-later, statically linked without GPL/nonfree/version3 options. '
        'Source: https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz (also attached to this release). '
        'See licenses/ffmpeg-configuration.txt and build_ffmpeg_static.sh. Rebuild this open-source application to relink.\n\n'
        'OmniCore Host 0.4.8: GPL-3.0; https://github.com/Liren66/OmniCore-SDR-Host. '
        'Its source snapshot is attached to this release; commit and DLL hash are in BUILD-INFO.json.\n\n'
        'libusbK 3.1.0.0: bundled USB transport; notices are in licenses/libusbK, '
        'and corresponding sources are in the OmniCore Host source snapshot.\n\n'
        'Whisper, GGML and codec2 are built from third_party/ in the application source archive; '
        'their notices are in THIRD_PARTY.md and LICENSES/. MSYS2 runtime dependency notices are in licenses/msys2/. '
        'MSYS2 source recipes: https://github.com/msys2/MINGW-packages; '
        'corresponding package versions are recorded in BUILD-INFO.json.\n', encoding='utf-8')
    subprocess.check_call([sys.executable, 'tools/package/scrub_paths.py', str(out)], cwd=ROOT)
    info = {
        'product': 'OmniCore-SDR-OnAir', 'version': VERSION,
        'source_commit': run('git', 'rev-parse', 'HEAD').strip(),
        'source_dirty': bool(run('git', 'status', '--porcelain', '--untracked-files=no').strip()),
        'upstream_commit': run('git', 'rev-parse', 'upstream/main').strip(),
        'host_commit': run('git', 'rev-parse', 'HEAD', cwd=source).strip(),
        'host_build_dll_sha256': sha(host_dll),
        'host_packaged_dll_sha256': sha(out / 'omnicore.dll'),
        'ffmpeg_source_sha256': sha(build / 'deps/ffmpeg-8.0.tar.xz'),
        'toolchain_packages': run('pacman', '-Q').splitlines(),
        'files_sha256': {p.name: sha(p) for p in sorted(out.glob('*')) if p.is_file()},
    }
    (out / 'BUILD-INFO.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    bash = shutil.which('bash')
    out_posix = run('cygpath', '-u', out).strip()
    for exe in binaries.values():
        subprocess.check_call([bash, 'tools/package/check_lgpl_bundle.sh', f'{out_posix}/{exe}', out_posix], cwd=ROOT)
    subprocess.check_call([bash, 'tools/package/check_win_dlls.sh', out_posix], cwd=ROOT)
    archive = Path(str(out) + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(release))
    print(archive)


if __name__ == '__main__':
    main()

import hashlib,json,os,re,shutil,subprocess,tarfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
version=re.search(r'project\(OnAir VERSION ([0-9.]+)', (root/'CMakeLists.txt').read_text()).group(1)
build=root/'build-release'
release=root/'release'
release.mkdir(exist_ok=True)
out=release/f'OmniCore-SDR-OnAir-{version}-linux-x86_64'
out.mkdir()
(out/'bin').mkdir(); (out/'lib').mkdir()
names={'dect2':'OmniCore-SDR-OnAir','dect2cli':'onair-cli','dect2scan':'onair-scan','onair-server':'onair-server','onair-plugin-host':'onair-plugin-host','omnicore_probe':'omnicore_probe'}
for src,dst in names.items():
    subprocess.run(['strip','-o',str(out/'bin'/dst),str(build/src)],check=True)
    wrapper=out/dst
    wrapper.write_text('#!/bin/sh\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport LD_LIBRARY_PATH="$base/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexec "$base/bin/'+dst+'" "$@"\n')
    wrapper.chmod(0o755)
for src,dst in [(build/'omnicore-host/libomnicore.so.0.4.8','libomnicore.so.0.4.8'),(build/'libcodec2.so.1.2.0','libcodec2.so.1.2.0')]:
    subprocess.run(['strip','-o',str(out/'lib'/dst),str(src)],check=True)
for name,target in [('libomnicore.so','libomnicore.so.0.4.8'),('libomnicore.so.1','libomnicore.so.0.4.8'),('libcodec2.so','libcodec2.so.1.2.0'),('libcodec2.so.1.2','libcodec2.so.1.2.0')]:
    (out/'lib'/name).symlink_to(target)
for name in ['LICENSE','README.md','README.upstream.md','THIRD_PARTY.md','DEVICES.md']:
    shutil.copy2(root/name,out/name)
shutil.copytree(root/'LICENSES',out/'LICENSES')
shutil.copytree(root/'packaging/icons',out/'icons')
shutil.copy2(root/'packaging/linux/install-omnicore-desktop.sh',out/'install-desktop.sh')
(out/'install-desktop.sh').chmod(0o755)
shutil.copy2(build/'omnicore-host-source/LICENSE',out/'LICENSES/OmniCore-Host-LICENSE.txt')
shutil.copy2(build/'deps/ffmpeg/share/ffmpeg-configuration.txt',out/'LICENSES/ffmpeg-configuration.txt')
shutil.copy2(root/'tools/package/build_ffmpeg_static.sh',out/'LICENSES/build_ffmpeg_static.sh')
shutil.copytree(root/'web/onair-web',out/'web/onair-web')
shutil.copy2(root/'packaging/linux/60-onair-sdr.rules',out/'60-onair-sdr.rules')
(out/'RUN-LINUX.txt').write_text(f'OmniCore-SDR-OnAir {version}, Ubuntu 24.04 x86-64\n\nRun ./OmniCore-SDR-OnAir from a graphical desktop, as your normal user.\nThe root-level launchers load the bundled OmniCore Host 0.4.8 and codec2.\nSystem dependencies: sudo apt install libglfw3 libhackrf0 libusb-1.0-0 libasound2t64 libgl1\nUSB access: install the included 60-onair-sdr.rules in /etc/udev/rules.d/, reload udev rules and reconnect.\nUse the normal desktop account (hzz on the tested machine); do not launch with sudo.\nUse ./omnicore_probe --help for manual diagnostics; --tx enables actual RF transmission.\n\nSources: https://github.com/Liren66/OmniCore-SDR-OnAir\nOmniCore Host source: https://github.com/Liren66/OmniCore-SDR-Host/tree/894ffd073ba0cf80312376f90e16e40ba1d88351\nFFmpeg source: https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz\nRun ./install-desktop.sh to add the original OnAir icon to your application menu.\nIcon copyright is separate from the code licence; see THIRD_PARTY.md.\nFFmpeg is static LGPL, without GPL/nonfree/version3 options; rebuild the open-source application to relink.\nSee LICENSE, THIRD_PARTY.md and LICENSES for notices.\n')
run=lambda *args: subprocess.check_output(args,cwd=root,text=True).strip()
sha=lambda p: hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
info={'product':'OmniCore-SDR-OnAir','version':version,'platform':'Ubuntu 24.04 x86-64','source_commit':run('git','rev-parse','HEAD'),'source_dirty':bool(run('git','status','--porcelain','--untracked-files=no')),'host_version':'0.4.8','host_source_archive_sha256':sha(build/'sources/OmniCore-Host-0.4.8-source.tar.gz') if (build/'sources/OmniCore-Host-0.4.8-source.tar.gz').exists() else None,'files_sha256':{str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and not p.is_symlink()}}
(out/'BUILD-INFO.json').write_text(json.dumps(info,indent=2)+'\n')
archive=Path(str(out)+'.tar.gz')
with tarfile.open(archive,'w:gz') as tar: tar.add(out,arcname=out.name)
print(archive)
print(sha(archive))

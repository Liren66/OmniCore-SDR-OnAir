#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$base" <<'PY'
from pathlib import Path
import os
import shutil
import sys

base = Path(sys.argv[1]).resolve()
data = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
apps = data / 'applications'
icons = data / 'icons/hicolor/scalable/apps'
apps.mkdir(parents=True, exist_ok=True)
icons.mkdir(parents=True, exist_ok=True)
shutil.copy2(base / 'icons/onair.svg', icons / 'omnicore-sdr-onair.svg')
exe = str(base / 'OmniCore-SDR-OnAir')
for old, new in [('\\', '\\\\'), ('"', '\\"'), ('`', '\\`'), ('$', '\\$'), ('%', '%%')]:
    exe = exe.replace(old, new)
entry = apps / 'omnicore-sdr-onair.desktop'
entry.write_text('[Desktop Entry]\nType=Application\nName=OmniCore-SDR-OnAir\n'
                 'Comment=OnAir fork with native OmniCore SDR RX and TX\n'
                 f'Exec="{exe}"\nIcon=omnicore-sdr-onair\nTerminal=false\n'
                 'StartupWMClass=OmniCore-SDR-OnAir\nCategories=AudioVideo;HamRadio;\n',
                 encoding='utf-8')
print(f'Installed application menu entry: {entry}')
PY

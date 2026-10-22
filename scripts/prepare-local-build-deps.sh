#!/usr/bin/env bash
# Unpack Ubuntu build dependencies locally; no system package installation.
set -euo pipefail
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$PROJECT_DIR/.local-build/deps"
ARCHIVES_DIR="$PROJECT_DIR/.local-build/dependency-archives"
mkdir -p "$DEPS_DIR" "$ARCHIVES_DIR"
cd "$ARCHIVES_DIR"
apt-get download xfce4-dev-tools libexo-2-dev libxfce4ui-2-dev libxfce4util-dev \
  libxfconf-0-dev gtk-doc-tools libgudev-1.0-dev libudev-dev xsltproc docbook-xsl
for archive in ./*.deb; do
  dpkg-deb --extract "$archive" "$DEPS_DIR"
done

python3 - "$DEPS_DIR" <<'PY'
from pathlib import Path
import os
import re
import sys

root = Path(sys.argv[1])
prefix = str(root / 'usr')
for pc in root.rglob('*.pc'):
    text = pc.read_text().replace('=/usr', '=' + prefix)
    pc.write_text(text)

# Development-package linker symlinks point at runtime libraries already
# installed for the distro Thunar. Reuse those exact runtime files.
for link in (root / 'usr/lib').rglob('*'):
    if link.is_symlink() and not link.exists():
        system_link = Path('/') / link.relative_to(root)
        if system_link.exists():
            target = system_link.resolve()
        else:
            target = system_link.parent / os.readlink(link)
        if target.exists():
            link.unlink()
            link.symlink_to(target)

# These distro scripts embed their data directories. Relocate only the local
# copies, including gtk-doc's Python configuration module when present.
candidates = [root / 'usr/bin/gtkdocize', root / 'usr/bin/xdt-autogen']
candidates.extend((root / 'usr/share/gtk-doc').rglob('*.py'))
for path in candidates:
    if not path.is_file():
        continue
    text = path.read_text()
    text = re.sub(r'''(?m)^(prefix\s*=\s*)(["']?)/usr\2$''',
                  lambda match: match[1] + match[2] + prefix + match[2], text)
    for directory in ('/usr/share/gtk-doc', '/usr/share/xfce4'):
        text = text.replace(directory, str(root) + directory)
    path.write_text(text)
PY

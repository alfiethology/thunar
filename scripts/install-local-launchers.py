#!/usr/bin/env python3
"""Install personal Thunar launchers after a successful local build."""

import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dry-run', action='store_true', help='validate menu changes without writing launchers')
args = parser.parse_args()
project = Path(__file__).resolve().parent.parent
home = Path.home()
state = project / '.local-build'
launcher = project / 'launch_thunar.sh'
binary = state / 'install/bin/thunar'
if not args.dry_run and not os.access(binary, os.X_OK):
    raise SystemExit(f'Missing built executable: {binary}')

backup = state / 'launcher-backups' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
manifest = []


def install(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    entry = {'path': str(path), 'backup': None}
    if path.exists() or path.is_symlink():
        target = backup / str(len(manifest))
        shutil.copy2(path, target, follow_symlinks=False)
        entry['backup'] = str(target)
    manifest.append(entry)
    (backup / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    descriptor, name = tempfile.mkstemp(prefix='.thunar-setup-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as handle:
            handle.write(content)
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


applications = home / '.local/share/applications'
desktop_entries = []
for filename in ('thunar.desktop', 'thunar-bulk-rename.desktop'):
    target = applications / filename
    source = target if target.exists() else Path('/usr/share/applications') / filename
    content = re.sub(r'^Exec=(?:/usr/bin/)?(?:thunar|Thunar)(?=\s|$)',
                     f'Exec={launcher}', source.read_text(), flags=re.MULTILINE)
    # Preserve custom actions, repairing case mismatches in their identifiers.
    groups = re.findall(r'^\[Desktop Action (.+)\]$', content, re.MULTILINE)
    def repair_actions(match):
        actions = match.group(1).split(';')
        for index, action in enumerate(actions):
            if action and action not in groups:
                candidates = [group for group in groups if group.casefold() == action.casefold()]
                if len(candidates) == 1:
                    actions[index] = candidates[0]
        return 'Actions=' + ';'.join(actions)
    content = re.sub(r'^Actions=(.*)$', repair_actions, content, flags=re.MULTILINE)
    # Validate before replacing any existing menu entry, including custom actions.
    with tempfile.NamedTemporaryFile(mode='w', suffix='.desktop') as temporary:
        temporary.write(content)
        temporary.flush()
        subprocess.run(['desktop-file-validate', temporary.name], check=True)
    desktop_entries.append((target, content))

# Validate every menu entry before changing the user's launchers.
if args.dry_run:
    for target, content in desktop_entries:
        print(f'Validated: {target}')
        for line in content.splitlines():
            if line.startswith(('Exec=', 'Actions=')):
                print(line)
    raise SystemExit(0)

backup.mkdir(parents=True)
# Use wrappers instead of symlinks so launch_thunar.sh resolves its own directory.
for command in ('thunar', 'Thunar'):
    install(home / '.local/bin' / command,
            f'#!/bin/sh\nexec "{launcher}" "$@"\n', 0o755)
for target, content in desktop_entries:
    install(target, content)

services = {
    'org.xfce.Thunar.service': 'org.xfce.Thunar',
    'org.xfce.FileManager.service': 'org.xfce.FileManager',
    'org.xfce.Thunar.FileManager1.service': 'org.freedesktop.FileManager1',
    'org.freedesktop.FileManager1.service': 'org.freedesktop.FileManager1',
}
for filename, name in services.items():
    install(home / '.local/share/dbus-1/services' / filename,
            f'[D-BUS Service]\nName={name}\nExec={launcher} --gapplication-service\n'
            'SystemdService=thunar.service\n')

install(home / '.config/systemd/user/thunar.service.d/local-checkout.conf',
        f'[Service]\nExecStart=\nExecStart={launcher} --daemon\n')
subprocess.run(['update-desktop-database', str(applications)], check=True)
subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)

# Ask the session bus to refresh activation files without interrupting open windows.
subprocess.run(['dbus-send', '--session', '--type=method_call', '--print-reply',
                '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus',
                'org.freedesktop.DBus.ReloadConfig'], check=True)
print(f'Previous personal launcher settings saved to {backup}')
print('Personal menu, command and D-Bus launchers now target this checkout.')
print('Existing Thunar windows keep their current process until Thunar is restarted.')

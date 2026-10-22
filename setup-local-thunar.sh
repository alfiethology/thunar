#!/usr/bin/env bash
# Build this checkout, verify it, and switch personal launchers only on success.
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="$PROJECT_DIR/.local-build"
INSTALL_DIR="$STATE_DIR/install"
mkdir -p "$STATE_DIR"
exec 9>"$STATE_DIR/setup.lock"
flock -n 9 || { echo 'Another Thunar setup is already running.' >&2; exit 1; }
printf '%s\n' "$$" > "$STATE_DIR/setup.pid"
rm -f "$STATE_DIR/setup.exit-status"
printf 'running\n' > "$STATE_DIR/setup.status"
trap 'result=$?; printf "%s\n" "$result" > "$STATE_DIR/setup.exit-status"; if (( result == 0 )); then printf "complete\n" > "$STATE_DIR/setup.status"; else printf "failed\n" > "$STATE_DIR/setup.status"; fi' EXIT

if [[ "${1:-}" == '--install-deps' ]]; then
  sudo -n apt-get update
  sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential autoconf automake libtool intltool xfce4-dev-tools \
    gtk-doc-tools libexo-2-dev libxfce4ui-2-dev libxfce4util-dev libxfconf-0-dev \
    libgtk-3-dev libgudev-1.0-dev libnotify-dev libsm-dev libice-dev \
    libgirepository1.0-dev libxml2-utils xsltproc docbook-xsl ripgrep
fi

if [[ "${1:-}" == '--local-deps' ]]; then
  bash "$PROJECT_DIR/scripts/prepare-local-build-deps.sh"
fi

DEPS_DIR="$STATE_DIR/deps"
if [[ -d "$DEPS_DIR/usr" ]]; then
  export PATH="$DEPS_DIR/usr/bin:$PATH"
  export PKG_CONFIG_PATH="$DEPS_DIR/usr/lib/$(gcc -dumpmachine)/pkgconfig:$DEPS_DIR/usr/lib/pkgconfig:$DEPS_DIR/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  export ACLOCAL_FLAGS="-I $DEPS_DIR/usr/share/aclocal ${ACLOCAL_FLAGS:-}"
  export PYTHONPATH="$DEPS_DIR/usr/share/gtk-doc/python${PYTHONPATH:+:$PYTHONPATH}"
  export XML_CATALOG_FILES="$DEPS_DIR/usr/share/xml/docbook/stylesheet/docbook-xsl/catalog.xml /etc/xml/catalog"
fi

cd "$PROJECT_DIR"
./autogen.sh --prefix="$INSTALL_DIR" --disable-gtk-doc --disable-introspection
jobs="$(nproc)"
(( jobs <= 4 )) || jobs=4
make -j"$jobs"
make check
make install

# --version does not start a desktop session or talk to the running instance.
"$PROJECT_DIR/launch_thunar.sh" --version
python3 "$PROJECT_DIR/scripts/install-local-launchers.py"
printf '\nBuild, tests, installation and launcher setup succeeded.\n'
printf 'Launcher: %s/launch_thunar.sh\n' "$PROJECT_DIR"
printf 'Rebuild after source edits: %s/setup-local-thunar.sh\n' "$PROJECT_DIR"

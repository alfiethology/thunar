#!/usr/bin/env bash
set -euo pipefail
source .github/scripts/release-version.sh
test "$(uname -m)" = arm64

# Thunar requires X11. Use MacPorts' X11 GTK, rather than Homebrew's Quartz GTK.
macports_version=2.12.7
installer="MacPorts-$macports_version-15-Sequoia.pkg"
curl --fail --location --retry 3 \
  "https://github.com/macports/macports-base/releases/download/v$macports_version/$installer" \
  -o "$RUNNER_TEMP/$installer"
pkgutil --check-signature "$RUNNER_TEMP/$installer"
sudo installer -pkg "$RUNNER_TEMP/$installer" -target /
export PATH="/opt/local/bin:/opt/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin"
sudo port -N selfupdate
sudo port -N install gtk3 +x11 -quartz
sudo port -N install xfce4-dev-tools intltool gtk-doc pkgconfig \
  autoconf automake libtool dbus-glib libnotify libexif \
  xorg-libSM xorg-libICE startup-notification ripgrep adwaita-icon-theme

# MacPorts' Xfce libraries can lag behind this checkout's minimum versions.
# Build a private, compatible Xfce stack and include its sources in the package.
prefix=/opt/thunar
sudo mkdir -p "$prefix"
sudo chown "$(id -u):$(id -g)" "$prefix"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:/opt/local/lib/pkgconfig"
export CPPFLAGS="-I$prefix/include -I/opt/local/include"
export LDFLAGS="-L$prefix/lib -L/opt/local/lib"
export XDG_DATA_DIRS="$prefix/share:/opt/local/share:/usr/share"
mkdir -p "$RUNNER_TEMP/xfce-sources" "$prefix/share/thunar-release/sources"
for dependency in libxfce4util:4.18.2 xfconf:4.18.3 libxfce4ui:4.18.6 exo:4.18.0; do
  name=${dependency%:*}
  dep_version=${dependency#*:}
  archive="$name-$dep_version.tar.bz2"
  curl --fail --location --retry 3 \
    "https://archive.xfce.org/src/xfce/$name/4.18/$archive" \
    -o "$RUNNER_TEMP/xfce-sources/$archive"
  cp "$RUNNER_TEMP/xfce-sources/$archive" "$prefix/share/thunar-release/sources/"
  tar xf "$RUNNER_TEMP/xfce-sources/$archive" -C "$RUNNER_TEMP/xfce-sources"
  (
    cd "$RUNNER_TEMP/xfce-sources/$name-$dep_version"
    ./configure --prefix="$prefix" --disable-static --disable-gtk-doc \
      --disable-introspection --disable-vala --disable-gtktest
    make -j"$(sysctl -n hw.ncpu)"
    make install
  )
done

./autogen.sh --prefix="$prefix" --disable-static --disable-gtk-doc \
  --disable-introspection --disable-gudev
make -j"$(sysctl -n hw.ncpu)"
# GUI tests need a running X server; run the display-independent tests here.
make -C thunar test-rg-search test-recent
./thunar/test-rg-search
./thunar/test-recent
make install
"$prefix/bin/thunar" --version
file "$prefix/bin/thunar" | grep arm64
find "$prefix" -name '*.la' -delete
mkdir -p "$prefix/share/licenses/thunar"
cp COPYING COPYING.LIB "$prefix/share/licenses/thunar/"

# Launch with the packaged xfconf daemon, resources, and MacPorts runtime.
mv "$prefix/bin/thunar" "$prefix/bin/thunar-bin"
cat > "$prefix/bin/thunar" <<'LAUNCHER'
#!/bin/sh
export PATH="/opt/thunar/bin:/opt/local/bin:/opt/local/sbin:$PATH"
export XDG_DATA_DIRS="/opt/thunar/share:/opt/local/share:${XDG_DATA_DIRS:-/usr/share}"
exec /opt/local/bin/dbus-run-session -- /opt/thunar/bin/thunar-bin "$@"
LAUNCHER
chmod 755 "$prefix/bin/thunar"
mkdir -p dist
pkgbuild --root "$prefix" --install-location "$prefix" \
  --identifier org.xfce.thunar.custom --version "$version.$revision" \
  --ownership recommended "dist/thunar-$version-$revision-macos-15-arm64.pkg"

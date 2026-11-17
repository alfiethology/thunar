#!/usr/bin/env bash
set -euo pipefail
source .github/scripts/release-version.sh
source /etc/os-release
export DEBIAN_FRONTEND=noninteractive
case "$ID" in
  ubuntu|debian)
    apt-get update
    apt-get install -y --no-install-recommends \
      build-essential autoconf automake libtool pkg-config intltool gtk-doc-tools git file \
      xfce4-dev-tools libexo-2-dev libxfce4ui-2-dev libxfce4util-dev \
      libxfconf-0-dev libgtk-3-dev libgudev-1.0-dev libnotify-dev libexif-dev \
      libpcre2-dev libx11-dev libsm-dev libice-dev dpkg-dev \
      dbus-x11 xvfb xauth ripgrep ca-certificates
    ;;
  fedora)
    dnf install -y gcc make autoconf automake libtool pkgconf-pkg-config \
      intltool gtk-doc xfce4-dev-tools exo-devel libxfce4ui-devel \
      libxfce4util-devel xfconf-devel gtk3-devel libgudev-devel libnotify-devel \
      libexif-devel pcre2-devel libX11-devel libSM-devel libICE-devel \
      rpm-build dbus-daemon xorg-x11-server-Xvfb xorg-x11-xauth ripgrep which \
      git file findutils diffutils tar bzip2
    ;;
  arch)
    pacman -Syu --noconfirm --needed base-devel xfce4-dev-tools intltool \
      gtk-doc exo libxfce4ui libxfce4util xfconf gtk3 libgudev libnotify \
      libexif pcre2 libx11 libsm libice dbus xorg-server-xvfb xorg-xauth ripgrep \
      git glib2-devel shared-mime-info desktop-file-utils adwaita-icon-theme
    ;;
  *) echo "Unsupported build distribution: $ID" >&2; exit 1 ;;
esac

mkdir -p dist
git config --global --add safe.directory /src
libdir=/usr/lib
if [[ "$ID" == fedora ]]; then
  libdir=/usr/lib64
fi
./autogen.sh --prefix=/usr --libdir="$libdir" \
  --disable-static --disable-gtk-doc --disable-introspection
make -j"$(nproc)"
dbus-run-session -- xvfb-run -a make check
stage=$(mktemp -d)
make DESTDIR="$stage" install
find "$stage" -name '*.la' -delete
install -Dm644 COPYING "$stage/usr/share/licenses/thunar/COPYING"
install -Dm644 COPYING.LIB "$stage/usr/share/licenses/thunar/COPYING.LIB"

case "$ID" in
  ubuntu|debian)
    # Resolve shared-library dependencies against the target distro's database.
    mkdir -p debian "$stage/DEBIAN"
    printf 'Source: thunar\nSection: x11\nPriority: optional\nMaintainer: Thunar release builders <noreply@localhost>\n\nPackage: thunar\nArchitecture: any\nDescription: Thunar file manager\n' > debian/control
    mapfile -t binaries < <(find "$stage/usr" -type f -exec sh -c 'file -b "$1" | grep -q "ELF.*dynamically linked"' sh {} \; -print)
    dependencies=$(dpkg-shlibdeps --ignore-missing-info -O \
      -l"$stage/usr/lib" "${binaries[@]/#/-e}" | sed -n 's/^shlibs:Depends=//p')
    test -n "$dependencies"
    cat > "$stage/DEBIAN/control" <<EOF
Package: thunar
Version: $version-$revision
Architecture: $(dpkg --print-architecture)
Maintainer: Thunar release builders <noreply@localhost>
Section: x11
Priority: optional
Depends: $dependencies, ripgrep, xfconf, exo-utils, shared-mime-info, desktop-file-utils, adwaita-icon-theme
Provides: thunar-data (= $version-$revision), libthunarx-3-0 (= $version-$revision), libthunarx-3-dev (= $version-$revision)
Conflicts: thunar-data, libthunarx-3-0, libthunarx-3-dev
Replaces: thunar-data, libthunarx-3-0, libthunarx-3-dev
Description: Thunar file manager with advanced ripgrep search
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
EOF
    printf '#!/bin/sh\nset -e\nldconfig\nupdate-desktop-database -q || true\ngtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true\n' > "$stage/DEBIAN/postinst"
    printf '#!/bin/sh\nset -e\nldconfig\n' > "$stage/DEBIAN/postrm"
    chmod 755 "$stage/DEBIAN/postinst" "$stage/DEBIAN/postrm"
    dpkg-deb --root-owner-group --build "$stage" "dist/thunar-$version-$revision-$TARGET.deb"
    apt-get install -y "./dist/thunar-$version-$revision-$TARGET.deb"
    thunar --version
    if [[ "$TARGET" == ubuntu-24.04-amd64 ]]; then
      make dist
      # Ensure the release tarball includes the navigation test launcher.
      tar tf "thunar-$version.tar.bz2" | grep '/tests/test-search-navigation.sh$'
      cp "thunar-$version.tar.bz2" "dist/thunar-$version-source.tar.bz2"
    fi
    ;;
  fedora)
    topdir=$(mktemp -d)
    find "$stage/usr" \( -type f -o -type l \) -print | sed "s|^$stage||" > "$topdir/files.list"
    cat > "$topdir/thunar.spec" <<EOF
Name: Thunar
Version: $version
Release: $revision%{?dist}
Summary: Thunar file manager with advanced ripgrep search
License: GPL-2.0-or-later AND LGPL-2.0-or-later
Requires: ripgrep, xfconf, exo, shared-mime-info, desktop-file-utils, adwaita-icon-theme
Provides: thunar = %{version}-%{release}
%description
Thunar file manager with advanced ripgrep search.
%install
cp -a $stage/. %{buildroot}/
%files -f $topdir/files.list
%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig
EOF
    rpmbuild -bb --define "_topdir $topdir" "$topdir/thunar.spec"
    find "$topdir/RPMS" -name '*.rpm' -exec cp {} dist/ \;
    dnf install -y dist/*.rpm
    thunar --version
    ;;
  arch)
    # makepkg refuses to run as root. Package the tested staging tree as a user.
    useradd -m builder
    package_dir=$(mktemp -d)
    mv "$stage" "$package_dir/stage"
    cat > "$package_dir/PKGBUILD" <<EOF
pkgname=thunar
pkgver=$version
pkgrel=$revision
pkgdesc='Thunar file manager with advanced ripgrep search'
arch=('x86_64')
url='https://docs.xfce.org/xfce/thunar/start'
license=('GPL-2.0-or-later' 'LGPL-2.0-or-later')
depends=('exo' 'libxfce4ui' 'libxfce4util' 'xfconf' 'gtk3' 'libgudev' 'libnotify' 'libexif' 'pcre2' 'libx11' 'libsm' 'libice' 'ripgrep' 'shared-mime-info' 'desktop-file-utils' 'adwaita-icon-theme')
options=('!debug' '!strip')
package() {
  cp -a "\$startdir/stage/." "\$pkgdir/"
}
EOF
    chown -R builder:builder "$package_dir"
    runuser -u builder -- bash -c 'cd "$1"; makepkg --noconfirm' bash "$package_dir"
    cp "$package_dir"/*.pkg.tar.zst dist/
    pacman -U --noconfirm dist/*.pkg.tar.zst
    thunar --version
    ;;
esac

# Thunar release packages

## Make a release

The workflow is `.github/workflows/release.yml`. Push the workflow and scripts
to GitHub first. Run **Actions → Release packages → Run workflow** to build and
download test artifacts without creating a release.

For a release, set the version in `configure.ac.in`, commit it, and push a
matching tag. For the current source version:

```sh
git tag v4.18.8
git push origin v4.18.8
```

Use `v4.18.8-2` for a second packaging revision of the same source version.
Tags must match the source version; arbitrary tag names are rejected.
After **all** Linux and macOS jobs succeed, the workflow creates a **draft**
GitHub Release with packages, a generated source tarball, installation notes,
and `SHA256SUMS`. Review the artifacts and publish the draft in GitHub.
The built-in `GITHUB_TOKEN` is sufficient; no personal access token is needed.
Reruns can update a draft but refuse to modify a published release.

## Linux installation

Choose the package matching your distribution and CPU. These packages install
this checkout as `thunar`, replacing the distribution's Thunar installation.
They depend on system GTK/Xfce libraries and are **not universal binaries**.
Avoid installing a package built for a different distribution or release.

| System | Artifact | Install command |
| --- | --- | --- |
| Ubuntu 24.04, Linux Mint 22 | `*-ubuntu-24.04-amd64.deb` | `sudo apt install ./thunar-*-ubuntu-24.04-amd64.deb` |
| Ubuntu 24.04 on ARM64 | `*-ubuntu-24.04-arm64.deb` | `sudo apt install ./thunar-*-ubuntu-24.04-arm64.deb` |
| Debian 13 | `*-debian-13-amd64.deb` | `sudo apt install ./thunar-*-debian-13-amd64.deb` |
| Fedora 44 | `*.fc44.x86_64.rpm` | `sudo dnf install ./Thunar-*.fc44.x86_64.rpm` |
| Arch Linux | `*.pkg.tar.zst` | `sudo pacman -U ./thunar-*.pkg.tar.zst` |

For other distributions (including openSUSE), use the `*-source.tar.bz2`
archive and the source installation instructions in README.md. Development
libraries must meet the versions checked by `configure`. Binary compatibility
with Arch derivatives and future rolling releases is not guaranteed.

Linux builds run `make check` inside D-Bus and Xvfb, then install the resulting
package in both the build environment and a clean container and run
`thunar --version`. They include ripgrep as a runtime dependency
for this checkout's search features.

## Apple Silicon macOS (experimental)

The Apple Silicon job builds on macOS 15. This is an **unsigned, unnotarized**
package requiring macOS 15 or later, a standard MacPorts installation under
`/opt/local`, and an X11 server. It is not a standalone `.app` or `.dmg`.
The macOS path needs its first successful GitHub Actions build and a manual GUI
check on a Mac before it should be considered supported. The Linux workspace
cannot verify macOS compilation or desktop behavior.

1. Install [MacPorts](https://www.macports.org/install.php) for your macOS version.
2. Install the runtime dependencies and X11 server:

   ```sh
   sudo port selfupdate
   sudo port install gtk3 +x11 -quartz
   sudo port install dbus-glib libnotify libexif startup-notification ripgrep adwaita-icon-theme xorg-server
   ```

3. Install `thunar-*-macos-15-arm64.pkg` using Installer, or:

   ```sh
   sudo installer -pkg ./thunar-4.18.8-1-macos-15-arm64.pkg -target /
   ```

4. Open MacPorts' X11 application and run this from its terminal:

   ```sh
   /opt/thunar/bin/thunar
   ```

The package installs a private Xfce dependency stack under `/opt/thunar`.
Its matching dependency source archives are included under
`/opt/thunar/share/thunar-release/sources`. GTK, D-Bus, ripgrep and other
MacPorts libraries remain external dependencies. Existing Quartz-only GTK
installations may require resolving MacPorts variant conflicts first.

The workflow runs display-independent search/recent tests on macOS; it does
not run the GUI suite there. Linux device management integrations are disabled.

## Verify a download

Download `SHA256SUMS` alongside the release files. On Linux use
`sha256sum --ignore-missing -c SHA256SUMS`. On macOS use
`shasum -a 256 -c SHA256SUMS` (missing files are reported if you downloaded only
some of the assets). Checksums detect corrupted downloads; they are not code
signatures.

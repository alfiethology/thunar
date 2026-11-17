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
After **all** Linux jobs succeed, the workflow creates a **draft**
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

## Verify a download

Download `SHA256SUMS` alongside the release files and run
`sha256sum --ignore-missing -c SHA256SUMS`. Checksums detect corrupted
downloads; they are not code signatures.

[![License](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://gitlab.xfce.org/xfce/thunar/COPYING)

# thunar


Thunar is a modern file manager for the Xfce Desktop Environment. Thunar has been designed from the ground up to be fast and easy to use. Its user interface is clean and intuitive and does not include any confusing or useless options by default. Thunar starts up quickly and navigating through files and folders is fast and responsive.

----

### Homepage

[Thunar documentation](https://docs.xfce.org/xfce/thunar/start)

### Changelog

See [NEWS](https://gitlab.xfce.org/xfce/thunar/-/blob/master/NEWS) for details on changes and fixes made in the current release.

### Source Code Repository

[Thunar source code](https://gitlab.xfce.org/xfce/thunar)

### Download a Release Tarball

[Thunar archive](https://archive.xfce.org/src/xfce/thunar)
    or
[Thunar tags](https://gitlab.xfce.org/xfce/thunar/-/tags)

### Installation

From source: 

    % git clone https://gitlab.xfce.org/xfce/thunar
    % git checkout <branch|tag>  #optional step. Per default master is checked out
    % cd thunar
    % ./autogen.sh
    % make
    # make install

From release tarball:

    % tar xf thunar-<version>.tar.bz2
    % cd thunar-<version>
    % ./configure
    % make
    # make install

 Both autogen.sh and configure will list missing dependencies. 
 If your distribution provides development versions of the related packages, 
 install them. Otherwise you will need to build and install the missing dependencies from source.

### Filename search

The **File Size** column uses compact decimal units with one decimal place
(for example `23.7KB`). Folder totals are calculated in the background using
the same rules as Properties, including hidden files without following symlink
directories or crossing filesystems. At most two folder scans run per view;
navigation cancels them. `Calculating...` marks pending totals, `Unknown` marks
unreadable folders, and `≥` marks a partial total. Refresh to recalculate totals
after changes inside subfolders. Properties may use binary units (KiB, MiB), so
the same byte count can have a different displayed number there.

Local searches require ripgrep (`rg`) on `PATH`. They use `rg --files` to
enumerate files recursively from the current directory, then match only each
file's basename. File contents are never searched, and directories are not
returned. Matching keeps Thunar's case-insensitive, accent-insensitive literal
search terms (all whitespace-separated terms must match).

Local searches always include subdirectories. They include files excluded by
`.gitignore` or `.ignore`, ignore ripgrep's user configuration, and respect
Thunar's hidden-file setting, including backup files ending in `~`. Directory
symlinks are not followed. Remote and virtual folders retain Thunar's existing
search implementation and recursive-search preference.

Use **Go → Advanced Search…** or **Ctrl+Shift+F** for additional filters. The
same dialog is available from the **Advanced Search…** button below search
results. It supports filename words, wildcards (`*` and `?`), or regular
expressions; optional literal text inside files; minimum and maximum sizes in
KiB; inclusive modified-date ranges; case sensitivity; hidden files; and
subfolder searches. Empty filters match any value, and maximum size 0 means
unlimited. Filters are combined, and matching files appear in the normal view.

Advanced search works in local folders. Content search reads files only when
the content-text field is filled in; it uses ripgrep's normal text-search
behavior and does not extract text from formats such as PDF or Word documents.
Results are a snapshot: use Refresh to rerun the search. Each tab retains its
own filters until the search is cancelled. The ordinary Ctrl+F filename search
continues to avoid reading file contents.

Recent shows at most the 200 most recently used entries, newest first. The
limit is applied before loading target-file metadata, and searches within
Recent use that same set of 200 entries. Ordinary folders remain unlimited.

The ripgrep backend tests run with `make check`. They can also be built without
the Xfce development libraries:

```sh
cc -Wall -Wextra -Wno-unused-parameter -I. \
  tests/test-rg-search.c thunar/thunar-rg-search.c \
  $(pkg-config --cflags --libs gio-2.0) -o /tmp/test-thunar-rg-search
/tmp/test-thunar-rg-search
```

### Run this checkout on this computer

`./setup-local-thunar.sh --local-deps` downloads missing Ubuntu development
packages into `.local-build/deps`, builds and tests this checkout, and installs
it under `.local-build/install`. It then updates personal menu, command, and
D-Bus launchers to use `launch_thunar.sh`. Previous personal launcher files are
saved under `.local-build/launcher-backups`. Existing Thunar windows use their
original process until Thunar is restarted or you log out and back in.

After editing the source, run `./setup-local-thunar.sh` to rebuild and install
the changes. A rebuild is required for C source edits to take effect.

For additional build & debug hints, check the [Thunar wiki pages](https://wiki.xfce.org/thunar/dev) and the [detailed building wiki manual](https://docs.xfce.org/xfce/building).

### Reporting Bugs

Visit the [reporting bugs](https://docs.xfce.org/xfce/thunar/bugs) page to view currently open bug reports and instructions on reporting new bugs or submitting bugfixes.


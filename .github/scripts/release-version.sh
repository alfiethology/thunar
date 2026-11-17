#!/usr/bin/env bash
# Sourced by both builders. Release tags must match the source version.
version=$(sed -n 's/^m4_define(\[thunar_version_\(major\|minor\|micro\)\], \[\([0-9]*\)\]).*/\2/p' configure.ac.in | paste -sd . -)
revision=1
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Cannot read the version from configure.ac.in" >&2
  exit 1
fi
if [[ -n "${RELEASE_TAG:-}" ]]; then
  if [[ ! "$RELEASE_TAG" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-([0-9]+))?$ ]]; then
    echo "Use v$version or v$version-2 (numeric packaging revision)." >&2
    exit 1
  fi
  if [[ "${BASH_REMATCH[1]}" != "$version" ]]; then
    echo "Release tag $RELEASE_TAG does not match source version $version" >&2
    exit 1
  fi
  revision=${BASH_REMATCH[3]:-1}
fi

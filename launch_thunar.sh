#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$PROJECT_DIR/.local-build/install"

if [[ ! -x "$INSTALL_DIR/bin/thunar" ]]; then
  printf 'Thunar has not been built here yet. Run: %s/setup-local-thunar.sh\n' "$PROJECT_DIR" >&2
  exit 1
fi

export XDG_DATA_DIRS="$INSTALL_DIR/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$INSTALL_DIR/bin/thunar" "$@"

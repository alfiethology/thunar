#!/bin/sh
set -eu
command -v dbus-run-session >/dev/null 2>&1 || exit 77
navigation_test_root=$(mktemp -d /tmp/thunar-navigation-test-XXXXXX)
trap 'rm -rf -- "$navigation_test_root"' EXIT HUP INT TERM
mkdir -p "$navigation_test_root/config" "$navigation_test_root/data" "$navigation_test_root/cache" \
  "$navigation_test_root/desktop" "$navigation_test_root/home" "$navigation_test_root/bin"
touch "$navigation_test_root/desktop/the.txt" "$navigation_test_root/home/home.txt"
cat > "$navigation_test_root/bin/rg" <<'RG'
#!/bin/sh
printf './the.txt\0'
if [ -e "$THUNAR_NAVIGATION_SLOW_FILE" ]; then
  exec /bin/sleep 5
fi
RG
chmod +x "$navigation_test_root/bin/rg"
env XDG_CONFIG_HOME="$navigation_test_root/config" XDG_DATA_HOME="$navigation_test_root/data" \
  XDG_CACHE_HOME="$navigation_test_root/cache" PATH="$navigation_test_root/bin:$PATH" \
  THUNAR_NAVIGATION_SLOW_FILE="$navigation_test_root/slow-search" \
  dbus-run-session -- "${THUNAR_TEST_BUILD_DIR:-.}/test-search-navigation" \
  "$navigation_test_root/desktop" "$navigation_test_root/home"

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
release_dir="$repo_root/release/linux-x86_64"

os_name="$(uname -s)"
architecture="$(uname -m)"
if [[ "$os_name" != "Linux" || "$architecture" != "x86_64" ]]; then
    echo "Release binary target is Linux x86_64; current platform is $os_name/$architecture." >&2
    exit 1
fi

for tool in make strip sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Required tool not found: $tool" >&2
        exit 1
    }
done

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/zp84-release.XXXXXX")"
trap 'rm -rf -- "$tmp_dir"' EXIT
build_dir="$tmp_dir/build"

make -C "$repo_root" BUILD="$build_dir" all web
install -d -m 0755 "$release_dir"
install -m 0755 "$build_dir/zp84gui" "$release_dir/zp84gui"
install -m 0755 "$build_dir/zp84web" "$release_dir/zp84web"
install -m 0755 "$build_dir/zpsniff" "$release_dir/zpsniff"
install -m 0644 "$build_dir/zp84-ble.py" "$release_dir/zp84-ble.py"
install -m 0644 "$repo_root/scripts2/99-zp84.rules" "$release_dir/99-zp84.rules"
strip --strip-unneeded \
    "$release_dir/zp84gui" \
    "$release_dir/zp84web" \
    "$release_dir/zpsniff"

(
    cd "$repo_root"
    sha256sum \
        release/linux-x86_64/99-zp84.rules \
        release/linux-x86_64/README.md \
        release/linux-x86_64/zp84-ble.py \
        release/linux-x86_64/zp84gui \
        release/linux-x86_64/zp84web \
        release/linux-x86_64/zpsniff > release/SHA256SUMS
)

echo "Release files updated in $release_dir"
echo "Checksums written to $repo_root/release/SHA256SUMS"

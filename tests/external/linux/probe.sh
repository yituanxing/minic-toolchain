#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linux-discovery"}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
version=6.6.143
archive=${LINUX_ARCHIVE_CACHE:-"$work/linux-$version.tar.xz"}
src="$work/linux-$version"
out="$work/kbuild"
sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
url="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"

rm -rf "$work"
mkdir -p "$work" "$(dirname -- "$archive")"
if ! test -s "$archive" || ! printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c - >/dev/null 2>&1; then
    rm -f "$archive" "$archive.tmp"
    curl -fsSL "$url" -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" defconfig >"$work/defconfig.log" 2>&1
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -j4 V=1 init/main.o >"$work/gcc-reference.log" 2>&1
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" V=1 init/main.i >"$work/preprocess.log" 2>&1
input="$out/init/main.i"
test -s "$input"

python3 "$root/tools/dev/pr247-emitter-owner.py"
make -j4 -C "$root" MODE=release BUILD_DIR="$root/build/pr247-emitter-owner" >/dev/null
minic="$root/build/pr247-emitter-owner/bin/minic"

set +e
"$minic" -S "$input" -o "$work/init-main.s" >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e
printf '%s\n' "PR247_EMITTER_OWNER_STATUS=$status" >&2
cat "$work/minic.stderr" >&2
exit "$status"

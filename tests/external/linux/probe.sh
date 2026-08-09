#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linux-discovery"}
minic=${MINIC:-"$root/build/linux-compiler/bin/minic"}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
version=6.6.143
archive="$work/linux-$version.tar.xz"
src="$work/linux-$version"
out="$work/kbuild"
sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932

rm -rf "$work"
mkdir -p "$work"

curl -fsSL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" -o "$archive"
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

# Use Linux's own RISC-V configuration and Kbuild. No source/header substitutes and no
# handcrafted preprocessed input: GCC/Kbuild produces the exact translation unit MiniC sees.
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" defconfig \
    >"$work/defconfig.log" 2>&1

# Prove the selected source/config is valid with the normal target compiler first.
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -j4 V=1 init/main.o \
    >"$work/gcc-reference.log" 2>&1

# Ask Kbuild for the corresponding real preprocessed translation unit.
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" V=1 init/main.i \
    >"$work/preprocess.log" 2>&1

input="$out/init/main.i"
if test ! -s "$input"; then
    printf '%s\n' 'LINUX_PROBE_ERROR Kbuild did not produce init/main.i' >&2
    tail -n 120 "$work/preprocess.log" >&2
    exit 1
fi

set +e
"$minic" -S "$input" -o "$work/init-main.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*init\/main\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 18 ? frontier_line - 18 : 1))
    end_line=$((frontier_line + 18))
    printf '%s\n' "LINUX_BLOCKER release=$version source=init/main.c line=$frontier_line minic_status=$status" >&2
    printf '%s\n' "init/main.i frontier lines=$start_line-$end_line:" >&2
    nl -ba "$input" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC diagnostic:' >&2
    sed -n '1,160p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' "PASS external/linux release=$version arch=riscv config=defconfig source=init/main.c kbuild-preprocessed=1"

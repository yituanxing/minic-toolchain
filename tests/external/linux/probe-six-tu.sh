#!/bin/sh
set -eu

export LC_ALL=C
export LANG=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linux-six-tu"}
minic=${MINIC:-"$root/build/linux-compiler/bin/minic"}
cross_compile=${CROSS_COMPILE:-riscv64-linux-gnu-}
version=6.6.143
archive=${LINUX_ARCHIVE_CACHE:-"$work/linux-$version.tar.xz"}
src="$work/linux-$version"
out="$work/kbuild"
sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
url="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"
summary="$work/six-tu-summary.txt"

units='init/main.c
kernel/sched/core.c
mm/memory.c
fs/open.c
drivers/base/core.c
arch/riscv/kernel/process.c'

rm -rf "$work"
mkdir -p "$work/logs" "$work/minic"
mkdir -p "$(dirname -- "$archive")"

archive_valid=false
if test -s "$archive" && printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c - >/dev/null 2>&1; then
    archive_valid=true
    printf '%s\n' "LINUX_ARCHIVE_CACHE hit path=$archive"
fi
if test "$archive_valid" != true; then
    rm -f "$archive" "$archive.tmp"
    curl -fL --retry 5 --retry-delay 2 --retry-all-errors "$url" -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
    printf '%s\n' "LINUX_ARCHIVE_CACHE fill path=$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" defconfig \
    >"$work/defconfig.log" 2>&1

: > "$summary"
printf '%s\n' "release=$version" >> "$summary"
printf '%s\n' 'arch=riscv' >> "$summary"
printf '%s\n' 'config=defconfig' >> "$summary"

passed=0
for source in $units; do
    stem=${source%.c}
    object="$stem.o"
    preprocessed="$stem.i"
    safe_name=$(printf '%s' "$stem" | tr '/' '-')
    input="$out/$preprocessed"
    assembly="$work/minic/$stem.s"
    stdout="$work/logs/$safe_name.minic.stdout"
    stderr="$work/logs/$safe_name.minic.stderr"

    mkdir -p "$(dirname -- "$assembly")"
    printf '%s\n' "LINUX_SIX_TU_REFERENCE source=$source"
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" -j4 V=1 "$object" \
        >"$work/logs/$safe_name.gcc.log" 2>&1
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross_compile" V=1 "$preprocessed" \
        >"$work/logs/$safe_name.preprocess.log" 2>&1

    if test ! -s "$input"; then
        printf '%s\n' "LINUX_SIX_TU_ERROR source=$source missing=$preprocessed" >&2
        tail -n 120 "$work/logs/$safe_name.preprocess.log" >&2
        exit 1
    fi

    line_count=$(wc -l < "$input" | tr -d ' ')
    byte_count=$(wc -c < "$input" | tr -d ' ')
    printf '%s\n' "LINUX_SIX_TU_INPUT source=$source lines=$line_count bytes=$byte_count"
    printf '%s\n' "source=$source lines=$line_count bytes=$byte_count" >> "$summary"

    set +e
    "$minic" -S "$input" -o "$assembly" >"$stdout" 2>"$stderr"
    status=$?
    set -e

    if test "$status" -ne 0; then
        frontier_line=$(awk -F: '$2 ~ /^[0-9]+$/ { print $2; exit }' "$stderr")
        if test -z "$frontier_line"; then
            frontier_line=1
        fi
        start_line=$((frontier_line > 18 ? frontier_line - 18 : 1))
        end_line=$((frontier_line + 18))
        cp "$input" "$work/blocker.i"
        printf '%s\n' "source=$source" > "$work/blocker.txt"
        printf '%s\n' "line=$frontier_line" >> "$work/blocker.txt"
        printf '%s\n' "status=$status" >> "$work/blocker.txt"
        printf '%s\n' "passed_before_blocker=$passed" >> "$work/blocker.txt"
        printf '%s\n' "LINUX_SIX_TU_BLOCKER source=$source line=$frontier_line passed=$passed minic_status=$status" >&2
        printf '%s\n' "$source preprocessed frontier lines=$start_line-$end_line:" >&2
        nl -ba "$input" | sed -n "${start_line},${end_line}p" >&2
        printf '%s\n' 'MiniC diagnostic:' >&2
        sed -n '1,160p' "$stderr" >&2
        exit "$status"
    fi

    passed=$((passed + 1))
    printf '%s\n' "PASS external/linux-six-tu source=$source lines=$line_count passed=$passed/6"
done

printf '%s\n' "PASS external/linux-six-tu release=$version arch=riscv config=defconfig translation_units=$passed/6"

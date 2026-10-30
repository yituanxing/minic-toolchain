#!/bin/sh
# GNU -O2 oracle -> MiniC C-to-S -> GNU AS/LD + native execution.
# Static immutable record field facts include a bounded no-op inline getter;
# never turn mutable, volatile or potentially out-of-bounds reads into constants.
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-const-global-record
mkdir -p "$work"

for name in scalar return; do
    if [ "$name" = scalar ]; then
        source="$root/tests/compiler/c0/gnu_const_global_record_scalar.c"
        unresolved='minic_const_record_guard_failed'
    else
        source="$root/tests/compiler/c0/gnu_const_global_record_return.c"
        unresolved='minic_bad_vic_field'
    fi
    cc -E -P -std=gnu11 "$source" -o "$work/$name.i"
    cc -O2 -std=gnu11 "$source" -o "$work/$name.gnu"
    "$MINIC_CC" -S "$work/$name.i" --target=x86_64-linux \
        -o "$work/$name.minic.s"
    if grep -Fq "$unresolved" "$work/$name.minic.s"; then
        echo "GNU_CONST_GLOBAL_RECORD=FAIL $name emitted live constant guard" >&2
        exit 1
    fi
    for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$MINIC_CC" -S "$work/$name.i" --target="$target" \
            -o "$work/$name.$target.s"
        if grep -Fq "$unresolved" "$work/$name.$target.s"; then
            echo "GNU_CONST_GLOBAL_RECORD=FAIL $name $target guard" >&2
            exit 1
        fi
    done
    cc -c -x assembler "$work/$name.minic.s" -o "$work/$name.minic.o" \
        2>"$work/$name.as.log" || {
        sed -n '1,15p' "$work/$name.as.log" >&2
        exit 1
    }
    cc -no-pie -Wl,-z,noexecstack "$work/$name.minic.o" -o "$work/$name.minic"
    "$work/$name.gnu"
    "$work/$name.minic"
done

for variant in MUTABLE VOLATILE UNKNOWN_INDEX OUT_OF_RANGE; do
    source="$root/tests/compiler/c0/gnu_const_global_record_negative.c"
    cc -E -P -std=gnu11 -D"TEST_$variant" "$source" \
        -o "$work/$variant.i"
    if [ "$variant" != OUT_OF_RANGE ]; then
        if cc -O2 -std=gnu11 -D"TEST_$variant" -c "$source" \
            -o "$work/$variant.gnu.o" >"$work/$variant.gnu.log" 2>&1; then
            echo "GNU_CONST_GLOBAL_RECORD=FAIL GNU accepted $variant" >&2
            exit 1
        fi
        grep -Fq 'global record field is not immutable and fixed' \
            "$work/$variant.gnu.log"
    fi
    "$MINIC_CC" -S "$work/$variant.i" --target=x86_64-linux \
        -o "$work/$variant.minic.s"
    cc -c -x assembler "$work/$variant.minic.s" \
        -o "$work/$variant.minic.o" 2>"$work/$variant.as.log"
    if ! nm -u "$work/$variant.minic.o" | \
        grep -Fq minic_global_record_not_constant; then
        echo "GNU_CONST_GLOBAL_RECORD=FAIL MiniC deleted $variant guard" >&2
        exit 1
    fi
done

if [ -n "${MINIC_X86_DRM_EDID_FROZEN_I:-}" ]; then
    test -r "$MINIC_X86_DRM_EDID_FROZEN_I"
    "$MINIC_CC" -S "$MINIC_X86_DRM_EDID_FROZEN_I" \
        --target=x86_64-linux -fshort-wchar -funsigned-char \
        -fcf-protection=branch -mindirect-branch=thunk-extern \
        -mfunction-return=thunk-extern -mindirect-branch-register \
        -fpatchable-function-entry=16,16 -o "$work/drm_edid.minic.s"
    cc -c -x assembler "$work/drm_edid.minic.s" \
        -o "$work/drm_edid.minic.o" 2>"$work/drm_edid.as.log" || {
        sed -n '1,15p' "$work/drm_edid.as.log" >&2
        exit 1
    }
    if nm -u "$work/drm_edid.minic.o" | grep -Fq '__compiletime_assert_428'; then
        echo 'GNU_CONST_GLOBAL_RECORD=FAIL frozen drm_edid.o assert_428' >&2
        exit 1
    fi
fi

printf 'GNU_CONST_GLOBAL_RECORD=PASS GNU_O2=2 MiniC_targets=5 GNU_AS_LD=2 native_runtime=4 strict_negative=4 drm_edid=%s\n' \
    "${MINIC_X86_DRM_EDID_FROZEN_I:+checked}"

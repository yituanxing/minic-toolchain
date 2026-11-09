#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic-cc"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-owner-reachability
source_file="$root/tests/compiler/c0/static_local_owner_reachability.c"
mkdir -p "$work"
gcc -E -P -std=gnu11 -x c "$source_file" -o "$work/owner.i"

for target in x86_64 aarch64 loongarch64 riscv64; do
    "$minic" --target="$target-linux" -S "$work/owner.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    if grep -Fq 'minic_dead_only_missing_function' "$work/$target.s" ||
       grep -Fq 'minic_dead_call_only_missing_function' "$work/$target.s" ||
       grep -Fq '.data.owner_dead' "$work/$target.s" ||
       grep -Fq '.discard.addressable' "$work/$target.s"; then
        echo "STATIC_LOCAL_OWNER_FAIL target=$target dead-static-emitted" >&2
        exit 1
    fi
    for section in .data.owner_live .data.owner_forced .data.owner_top; do
        if ! grep -Fq "$section" "$work/$target.s"; then
            echo "STATIC_LOCAL_OWNER_FAIL target=$target missing=$section" >&2
            exit 1
        fi
    done
    # The live inline body is normally emitted as a specialization clone,
    # while its original __func__ backing object remains source-owned.
    grep -Fq '.Lminic_func_name_' "$work/$target.s"
    case "$target" in
        x86_64) gnu=gcc; nm=nm; readelf=readelf; emulator=() ;;
        aarch64) gnu=aarch64-linux-gnu-gcc; nm=aarch64-linux-gnu-nm; readelf=aarch64-linux-gnu-readelf; emulator=(qemu-aarch64) ;;
        loongarch64) gnu=loongarch64-linux-gnu-gcc-13; nm=loongarch64-linux-gnu-nm; readelf=loongarch64-linux-gnu-readelf; emulator=(qemu-loongarch64) ;;
        riscv64) gnu=riscv64-linux-gnu-gcc; nm=riscv64-linux-gnu-nm; readelf=riscv64-linux-gnu-readelf; emulator=(qemu-riscv64) ;;
    esac
    if ! command -v "$gnu" >/dev/null || ! command -v "$nm" >/dev/null ||
       ! command -v "$readelf" >/dev/null; then
        if [[ "${MINIC_REQUIRE_FOUR_GNU:-0}" == 1 ]]; then
            echo "STATIC_LOCAL_OWNER_FAIL target=$target GNU-oracle-unavailable" >&2
            exit 1
        fi
        echo "STATIC_LOCAL_OWNER=ISA_PASS target=$target GNU-oracle-unavailable"
        continue
    fi
    # At -O0 GCC deliberately emits some dead non-inline static functions;
    # compare optimized reachability with MiniC's existing Core pruning.
    "$gnu" -O2 -fno-pie -c -x c "$source_file" -o "$work/$target.gnu.o"
    "$gnu" -c -x assembler "$work/$target.s" -o "$work/$target.minic.o"
    for producer in gnu minic; do
        "$nm" -u "$work/$target.$producer.o" >"$work/$target.$producer.undefined"
        "$readelf" -SW "$work/$target.$producer.o" >"$work/$target.$producer.sections"
        if grep -Fq 'minic_dead_only_missing_function' "$work/$target.$producer.undefined" ||
           grep -Fq 'minic_dead_call_only_missing_function' "$work/$target.$producer.undefined" ||
           grep -Fq '.data.owner_dead' "$work/$target.$producer.sections" ||
           grep -Fq '.discard.addressable' "$work/$target.$producer.sections"; then
            echo "STATIC_LOCAL_OWNER_FAIL target=$target owner=$producer dead-relocation" >&2
            exit 1
        fi
        for section in .data.owner_live .data.owner_forced .data.owner_top; do
            grep -Fq "$section" "$work/$target.$producer.sections"
        done
    done
    if [[ "$target" == x86_64 ]] || command -v "${emulator[0]}" >/dev/null; then
        "$gnu" -static -no-pie "$work/$target.gnu.o" -o "$work/$target.gnu"
        "$gnu" -static -no-pie "$work/$target.minic.o" -o "$work/$target.minic"
        for producer in gnu minic; do
            set +e
            timeout 30s "${emulator[@]}" "$work/$target.$producer" \
                >"$work/$target.$producer.stdout" 2>"$work/$target.$producer.stderr"
            rc=$?
            set -e
            if [[ "$rc" != 0 ]]; then
                echo "STATIC_LOCAL_OWNER_FAIL target=$target owner=$producer exit=$rc" >&2
                exit 1
            fi
        done
        cmp "$work/$target.gnu.stdout" "$work/$target.minic.stdout"
        cmp "$work/$target.gnu.stderr" "$work/$target.minic.stderr"
    fi
    echo "STATIC_LOCAL_OWNER=DIFF_PASS target=$target dead=absent live=present used=retained top=retained"
done

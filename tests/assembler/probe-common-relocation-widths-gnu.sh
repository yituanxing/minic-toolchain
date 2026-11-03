#!/usr/bin/env bash
# COMMON fixed-width symbol expression -> Target relocation capability ->
# ObjectModel -> ELF -> GNU ld -r. GNU decides which widths are representable
# for each ISA: a target without ADD/SUB or PC8 must reject those expressions
# rather than manufacturing an ELF relocation that cannot be linked.
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head MiniAS executable}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
mkdir -p "$work/gnu" "$work/mini"
if [[ ${MINIAS_DEBUG_WORK:-0} == 1 ]]; then
    echo "MINIAS_COMMON_RELOC_WORK=$work"
else
    trap 'rm -rf -- "$work"' EXIT
fi
for tool in as ld readelf python3; do command -v "$tool" >/dev/null; done

has_semantics=0
if python3 -c 'import elftools' >/dev/null 2>&1; then
    has_semantics=1
elif [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
    echo 'MINIAS_COMMON_RELOC_WIDTHS=BLOCKED pyelftools_required' >&2
    exit 2
fi

cat >"$work/prefix.s" <<'ASM'
.section .minias_widths,"",@progbits
.zero 7
.section .data.alpha,"aw",@progbits
.zero 3
ASM

observed=0 accepted=0 rejected=0 linked=0
test_profile() {
    local profile=$1 target=$2 klass=$3 machine=$4 gas=$5 linker=$6
    local gasflags=$7 ldflags=$8
    local -a gnu_args=() link_args=() mini_args=()
    read -r -a gnu_args <<<"$gasflags"
    read -r -a link_args <<<"$ldflags"
    case "$profile" in
      x86_64) mini_args=(--target=x86_64 -march=x86-64 -mabi=sysv) ;;
      i386) mini_args=(--target=x86_64 -march=i386 -mabi=i386) ;;
      rv32) mini_args=(--target=riscv -march=rv32i2p1 -mabi=ilp32) ;;
      rv64) mini_args=(--target=riscv -march=rv64i2p1 -mabi=lp64) ;;
      aarch64) mini_args=(--target=aarch64 -march=armv8-a -mabi=lp64) ;;
      loong) mini_args=(--target=loongarch64 -march=loongarch64 -mabi=lp64d) ;;
    esac
    if ! command -v "$gas" >/dev/null ||
       ! command -v "$linker" >/dev/null; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_COMMON_RELOC_WIDTHS=BLOCKED profile=$profile GNU_cross_missing=$gas/$linker" >&2
            return 2
        fi
        echo "MINIAS_COMMON_RELOC_WIDTHS=SKIP profile=$profile GNU_cross_unavailable"
        return 0
    fi
    "$gas" "${gnu_args[@]}" -o "$work/prefix.$profile.o" "$work/prefix.s"
    for width in 1 2 4 8; do
        local directive
        case "$width" in
          1) directive=.byte ;;
          2) directive=.short ;;
          4) directive=.long ;;
          8) directive=.quad ;;
        esac
        for expr in difference pc_relative absolute; do
            local source="$work/$profile-$width-$expr.s"
            # GNU ld -r synthesizes STT_FILE entries from input object
            # basenames. Equal basenames keep the strict symbol oracle about
            # assembly semantics, not the producer's temporary filename.
            local gnu_object="$work/gnu/$profile-$width-$expr.o"
            local mini_object="$work/mini/$profile-$width-$expr.o"
            local gnu_linked="$work/$profile-$width-$expr.gnu.linked.o"
            local mini_linked="$work/$profile-$width-$expr.mini.linked.o"
            local operand
            case "$expr" in
              difference) operand='a_owner-b_owner' ;;
              pc_relative) operand='a_owner-.' ;;
              absolute) operand='a_owner' ;;
            esac
            cat >"$source" <<ASM
.section .data.alpha,"aw",@progbits
.zero 3
a_owner:
.byte 0x55
.section .rodata.beta,"a",@progbits
.zero 5
b_owner:
.byte 0x66
.section .minias_widths,"",@progbits
.globl fixed_width_$expr
fixed_width_$expr:
$directive $operand
ASM
            local gnu_status=0 mini_status=0
            "$gas" "${gnu_args[@]}" -o "$gnu_object" "$source" \
                >"$work/gnu.log" 2>&1 || gnu_status=$?
            "$MINIAS" "${mini_args[@]}" -o "$mini_object" "$source" \
                >"$work/mini.log" 2>&1 || mini_status=$?
            ((observed+=1))
            if [[ $gnu_status -ne 0 && $mini_status -ne 0 ]]; then
                test ! -e "$mini_object"
                ((rejected+=1))
                continue
            fi
            if [[ $gnu_status -ne 0 || $mini_status -ne 0 ]]; then
                echo "MINIAS_COMMON_RELOC_WIDTHS=FAIL profile=$profile width=$width expr=$expr GNU_rc=$gnu_status MiniAS_rc=$mini_status" >&2
                sed -n '1,15p' "$work/gnu.log" >&2
                sed -n '1,15p' "$work/mini.log" >&2
                return 1
            fi
            test -s "$gnu_object" && test -s "$mini_object"
            for object in "$gnu_object" "$mini_object"; do
                readelf -h "$object" >"$work/header.txt"
                grep -Eq "Class:[[:space:]]+ELF$klass" "$work/header.txt"
                grep -Eq "Machine:.*$machine" "$work/header.txt"
                readelf -SW "$object" | grep -q '\.minias_widths'
            done
            ((accepted+=1))
            # This semantic comparator normalizes section identities,
            # relocation addends and linker-editable bits. It deliberately
            # does not demand identical ELF file bytes or table ordering.
            if [[ $has_semantics == 1 ]]; then
                if ! python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
                     "$gnu_object" "$mini_object" >"$work/strict.json"; then
                    echo "MINIAS_COMMON_RELOC_WIDTHS=FAIL semantic profile=$profile width=$width expr=$expr" >&2
                    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("failed_dimensions=", [k for k,v in d["dimensions"].items() if not v])' "$work/strict.json" >&2
                    sed -n '1,70p' "$work/strict.json" >&2
                    if [[ $profile == rv* ]]; then
                        echo 'GNU RISC-V attributes:' >&2
                        readelf -A "$gnu_object" >&2
                        echo 'MiniAS RISC-V attributes:' >&2
                        readelf -A "$mini_object" >&2
                    fi
                    return 1
                fi
            fi
            # GNU ld -r joins an independent preceding source object,
            # shifting section bases and exercising linker fixup semantics.
            "$linker" "${link_args[@]}" -r -o "$gnu_linked" \
                "$work/prefix.$profile.o" "$gnu_object"
            "$linker" "${link_args[@]}" -r -o "$mini_linked" \
                "$work/prefix.$profile.o" "$mini_object"
            if [[ $has_semantics == 1 ]]; then
                if ! python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
                    "$gnu_linked" "$mini_linked" >"$work/linked-strict.json"; then
                    echo "MINIAS_COMMON_RELOC_WIDTHS=FAIL linked profile=$profile width=$width expr=$expr" >&2
                    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("failed_dimensions=", [k for k,v in d["dimensions"].items() if not v])' "$work/linked-strict.json" >&2
                    sed -n '1,70p' "$work/linked-strict.json" >&2
                    return 1
                fi
            fi
            ((linked+=1))
        done
    done
    echo "MINIAS_COMMON_RELOC_WIDTHS=PASS profile=$profile tested=12 GNU_accept_or_reject=matched ELF$klass=1 ld_r=matched"
}

test_profile x86_64 x86_64 64 'Advanced Micro Devices X86-64' \
    as ld '--64' '-m elf_x86_64'
test_profile i386 x86_64 32 'Intel 80386' \
    as ld '--32' '-m elf_i386'
test_profile rv32 riscv 32 'RISC-V' \
    riscv64-linux-gnu-as riscv64-linux-gnu-ld '-march=rv32i2p1 -mabi=ilp32' '-m elf32lriscv'
test_profile rv64 riscv 64 'RISC-V' \
    riscv64-linux-gnu-as riscv64-linux-gnu-ld '-march=rv64i2p1 -mabi=lp64' '-m elf64lriscv'
test_profile aarch64 aarch64 64 'AArch64' \
    aarch64-linux-gnu-as aarch64-linux-gnu-ld '-march=armv8-a' ''
test_profile loong loongarch64 64 'LoongArch' \
    loongarch64-linux-gnu-as loongarch64-linux-gnu-ld '-mabi=lp64d' ''
echo "MINIAS_COMMON_RELOC_WIDTHS=PASS cases=$observed accepted=$accepted rejected=$rejected GNU_ld_r=$linked semantic_oracle=$has_semantics"

#!/usr/bin/env bash
# Exhaustive executable raw-data escape policy for explicit RISC-V ISA.
# Each entry mirrors one Common data_width()/add_data_stmt() directive;
# neither aliases nor variable-length encodings may bypass selected -march.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cases=(
    '.byte 0x13'
    '.half 0x0013'
    '.short 0x0013'
    '.hword 0x0013'
    '.value 0x0013'
    '.2byte 0x0013'
    '.word 0x00000013'
    '.long 0x00000013'
    '.4byte 0x00000013'
    '.dword 0x00000013'
    '.quad 0x00000013'
    '.xword 0x00000013'
    '.8byte 0x00000013'
    '.zero 4'
    '.space 4'
    '.skip 4'
    '.fill 1,4,0x13'
    '.uleb128 19'
    '.sleb128 19'
    '.asciz "A"'
    '.string "A"'
    '.ascii "A"'
)

count=0
for directive in "${cases[@]}"; do
    op=${directive%% *}
    printf '.text\n%s\n' "$directive" >"$work/input.s"
    for arch in rv64i rv64im rv32i rv32im; do
        if "$MINIAS" "-march=$arch" -o "$work/out.o" "$work/input.s" \
                >"$work/standard.out" 2>"$work/standard.err"; then
            printf 'RISC-V ISA raw-data escape succeeded: %s / %s\n' \
                "$arch" "$directive" >&2
            exit 1
        fi
        if ! grep -Fq "unsupported-isa-escape:$op-in-exec" "$work/standard.err"; then
            printf 'Wrong RISC-V ISA escape rejection for %s / %s\n' \
                "$arch" "$directive" >&2
            sed -n '1,12p' "$work/standard.err" >&2
            exit 1
        fi
        count=$((count + 1))
    done
    printf '.data\n%s\n' "$directive" >"$work/data.s"
    "$MINIAS" -march=rv64im -o "$work/data.o" "$work/data.s"
done

# GNU-style .org remains a separate executable-fragment escape;
# numeric LEB encodings are never implicitly trusted as instructions.
printf '.text\n.org 4\n' >"$work/input.s"
if "$MINIAS" -march=rv64im -o "$work/out.o" "$work/input.s" \
        >"$work/standard.out" 2>"$work/standard.err"; then
    echo 'RISC-V selected ISA accepted executable .org escape' >&2
    exit 1
fi
grep -Fq 'unsupported-isa-escape:.org' "$work/standard.err"

echo "MINIAS_RV_EXEC_RAW_ESCAPE=PASS arch_cases=$count data_valid=${#cases[@]}"

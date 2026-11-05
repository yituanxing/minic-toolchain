#!/usr/bin/env bash
# GNU `.ident` creates a real mergeable .comment MC DATA section, not a
# parser-only marker. Exercise section metadata, payload and source state.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"

work=$(mktemp -d)
trap 'status=$?; if ((status != 0)); then echo "MINIAS_COMMON_IDENT=FAIL status=$status" >&2; fi; rm -rf "$work"' EXIT

check_section() {
    local flavor="$1" section="$2"
    objcopy --only-section="$section" -O binary \
        "$work/$flavor-mini.o" "$work/$flavor-mini.bin"
    objcopy --only-section="$section" -O binary \
        "$work/$flavor-gnu.o" "$work/$flavor-gnu.bin"
    cmp "$work/$flavor-mini.bin" "$work/$flavor-gnu.bin"
}

check_case() {
    local flavor="$1"
    as --64 -o "$work/$flavor-gnu.o" "$work/$flavor.s"
    "$MINIAS" --target=x86_64 -o "$work/$flavor-mini.o" "$work/$flavor.s"
    check_section "$flavor" .comment
    check_section "$flavor" .text
    check_section "$flavor" .data
    if ! readelf -SW "$work/$flavor-mini.o" | \
            grep -Eq '[[:space:]]\.comment[[:space:]]+PROGBITS.*[[:space:]]01[[:space:]]+MS'; then
        echo "bad MiniAS .comment ELF attributes: $flavor" >&2
        return 1
    fi
}

cat >"$work/implicit.s" <<'SRC'
.data
.byte 0x11
.text
.globl ident_demo
ident_demo:
    nop
.ident "GCC: target-neutral payload"
.ident "assembler project"
.previous
.byte 0x22
.text
.ident ""
SRC
check_case implicit

# When source explicitly selected .comment before `.ident`, GAS does not
# add a second leading NUL: that seed is only created for implicit sections.
cat >"$work/explicit.s" <<'SRC'
.section .comment,"MS",@progbits,1
.ascii "prefix"
.text
.ident "GCC: later"
.data
.byte 0x7a
SRC
check_case explicit

# `.ident` is a machine-independent ELF directive. The four registered
# Targets must all preserve its bytes and mergeable section metadata; only
# the x86 result above claims an actual cross-object GNU differential.
cat >"$work/portable.s" <<'SRC'
.data
.byte 0x31
.ident "portable common comment"
.ident "second toolchain"
SRC
as --64 -o "$work/portable-gnu.o" "$work/portable.s"
for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target.o" "$work/portable.s"
    # Host GNU objcopy may support only x86; GNU readelf parses all ELF
    # machines. Compare the byte hexdumps independently of target ISA.
    cmp <(readelf -x .comment "$work/$target.o" | sed -n '/^  0x/p') \
        <(readelf -x .comment "$work/portable-gnu.o" | sed -n '/^  0x/p')
    if ! readelf -SW "$work/$target.o" | \
            grep -Eq '[[:space:]]\.comment[[:space:]]+PROGBITS.*[[:space:]]01[[:space:]]+MS'; then
        echo "bad cross-target .comment ELF attributes: $target" >&2
        exit 1
    fi
done

cat >"$work/bad.s" <<'SRC'
.ident not_a_string
SRC
if "$MINIAS" --target=x86_64 -o "$work/bad-mini.o" "$work/bad.s" \
        >"$work/bad.out" 2>&1; then
    echo "malformed `.ident` silently accepted" >&2
    exit 1
fi

echo 'MINIAS_COMMON_IDENT=PASS GNU_comment_data_and_metadata=2 common_targets=4 source_sections=1 malformed_rejected=1'

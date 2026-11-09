#!/usr/bin/env bash
# GNU chained [row][column] initializers must merge repeated row actions.
# All four cells of a populated row remain independent pointer relocations;
# later writes replace only the selected cell, including its relocation.
set -Eeuo pipefail
mini=${1:?MiniC compiler path}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/chained.i" <<'SOURCE'
extern int first, second, replacement, fourth;
extern int fifth, sixth, seventh, eighth;
void *const chained_grid[4][4] = {
    [1][0] = &first,
    [1][1] = &second,
    [1][0] = &replacement,
    [1][2] = &fourth,
    [2] = { &fifth, &sixth },
    [2][2] = &seventh,
    [2][3] = &eighth,
};
void *const flat_grid[2] = { &first, &second };
SOURCE

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$mini" --target="$target"-linux -S "$work/chained.i" -o "$work/$target.s"
    for symbol in replacement fourth fifth sixth seventh eighth; do
        test "$(grep -Ec "[[:space:]]$symbol$" "$work/$target.s")" -eq 1
    done
    test "$(grep -Ec '[[:space:]]second$' "$work/$target.s")" -eq 2
    test "$(grep -Ec '[[:space:]]first$' "$work/$target.s")" -eq 1
    grep -Fq 'flat_grid:' "$work/$target.s"
    grep -Fq 'chained_grid:' "$work/$target.s"
    if [[ "$target" == x86_64 ]]; then
        test "$(grep -Ec '[[:space:]]\.quad first$' "$work/$target.s")" -eq 1
        cc -c "$work/$target.s" -o "$work/$target.o"
        gcc -x c -c "$work/chained.i" -o "$work/gnu.o"
        test "$(readelf -rW "$work/$target.o" | grep -c 'R_X86_64_64')" -eq 9
        test "$(readelf -rW "$work/gnu.o" | grep -c 'R_X86_64_64')" -eq 9
    elif [[ "${MINIC_CHAINED_ARRAY_SOURCE_ONLY:-0}" != 1 ]]; then
        as_tool=${target}-linux-gnu-as
        command -v "$as_tool" >/dev/null
        "$as_tool" -o "$work/$target.o" "$work/$target.s"
    fi
done

cat > "$work/deep.i" <<'SOURCE'
extern int deep_a, deep_b;
void *const deep[2][2][2] = {
    [1][0][0] = &deep_a,
    [1][0][1] = &deep_b,
};
SOURCE
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$mini" --target="$target"-linux -S "$work/deep.i" -o "$work/deep-$target.s"
    test "$(grep -Ec '[[:space:]]deep_a$' "$work/deep-$target.s")" -eq 1
    test "$(grep -Ec '[[:space:]]deep_b$' "$work/deep-$target.s")" -eq 1
done

cat > "$work/invalid.i" <<'SOURCE'
extern int first;
void *const invalid[4][4] = { [4][0] = &first };
SOURCE
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    if "$mini" --target="$target"-linux -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/invalid-$target.log" 2>&1; then
        printf 'out-of-range chained designator accepted on %s\n' "$target" >&2
        exit 1
    fi
    test ! -e "$work/invalid-$target.s"
done
echo "MINIC_CHAINED_ARRAY_RELOC=PASS targets=5 grid_relocs=7 deep_relocs=2 overwritten=1 negative_rejected=5"

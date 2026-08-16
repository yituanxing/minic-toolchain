#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/target/target_info.c",
    '''    /* TargetConstraint v0 exposes the RV64 temporary-register class used by\n     * unchanged Linux. Broader physical-register classes remain fail-closed. */\n    return name_length == 2U && name[0] == 't' && name[1] >= '0' && name[1] <= '6';\n''',
    '''    /* TargetConstraint v1 keeps temporary registers available to the current\n     * inline-asm operand allocator while also recognizing the RV64 argument\n     * registers as clobber-only physical registers. The backend never allocates\n     * a0..a7 to GNU asm operands, so declaring them clobbered cannot alias an\n     * allocator-owned operand. Broader saved/special register classes remain\n     * fail-closed until their preservation contract is explicit. */\n    return name_length == 2U &&\n           ((name[0] == 't' && name[1] >= '0' && name[1] <= '6') ||\n            (name[0] == 'a' && name[1] >= '0' && name[1] <= '7'));\n''',
)

anchor = '''cat >"$work/unsupported-clobber.c" <<'EOF'\n'''
positive = '''cat >"$work/argument-clobber.c" <<'EOF'\nint f(int value) {\n    __asm__ __volatile__("add %0, %0, zero" : "+r"(value) : : "a0", "a7");\n    return value;\n}\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/argument-clobber.c" -o "$work/argument-clobber.i"\n"$minic" -S "$work/argument-clobber.i" -o "$work/argument-clobber.s"\ngrep -F 'add t0, t0, zero' "$work/argument-clobber.s" >/dev/null\n\n'''
replace_once(
    "tests/compiler/c0/run-gnu-inline-asm-operands.sh",
    anchor,
    positive + anchor,
)

print("staged RV64 argument-register GNU asm clobber support")

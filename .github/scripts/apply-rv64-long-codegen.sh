#!/usr/bin/env bash
set -Eeuo pipefail

python3 - <<'PY'
from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: marker count={text.count(old)}")
    p.write_text(text.replace(old, new, 1))

replace_once(
    'src/target/riscv64/codegen_support.c',
    '    *width = minic_type_is_char_integer(type) ? 1U : 4U;\n',
    '    *width = minic_type_is_char_integer(type) ? 1U\n'
    '             : minic_type_is_long_integer(type) ? 8U\n'
    '                                                : 4U;\n',
)
replace_once(
    'src/target/riscv64/codegen_support.c',
    '''    if (minic_type_is_char_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lbu" : "lb";
    }
    return minic_type_is_unsigned_integer(type) ? "lwu" : "lw";
''',
    '''    if (minic_type_is_char_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lbu" : "lb";
    }
    if (minic_type_is_long_integer(type)) {
        return "ld";
    }
    return minic_type_is_unsigned_integer(type) ? "lwu" : "lw";
''',
)
replace_once(
    'src/target/riscv64/codegen_support.c',
    '    return minic_type_is_char_integer(type) ? "sb" : "sw";\n',
    '    return minic_type_is_char_integer(type) ? "sb"\n'
    '           : minic_type_is_long_integer(type) ? "sd"\n'
    '                                              : "sw";\n',
)
replace_once(
    'src/target/riscv64/codegen_support.c',
    '''    if (minic_type_is_unsigned_integer(type)) {
        return fprintf(file,
''',
    '''    if (minic_type_is_long_integer(type)) {
        return true;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return fprintf(file,
''',
)

replace_once(
    'src/target/riscv64/codegen_function.c',
    '    *scalar_width = minic_type_is_char_integer(type) ? 1U : 4U;\n',
    '    *scalar_width = minic_type_is_char_integer(type) ? 1U\n'
    '                    : minic_type_is_long_integer(type) ? 8U\n'
    '                                                       : 4U;\n',
)
replace_once(
    'src/target/riscv64/codegen_function.c',
    '    directive = minic_type_is_char_integer(scalar_type) ? ".byte" : ".word";\n',
    '    directive = minic_type_is_char_integer(scalar_type) ? ".byte"\n'
    '                : minic_type_is_long_integer(scalar_type) ? ".dword"\n'
    '                                                         : ".word";\n',
)

expression = Path('src/target/riscv64/codegen_expression.c')
text = expression.read_text()
normalize_marker = '''static bool
minic_riscv64_emit_normalize_integer(FILE *file, MinicType type, const char *register_name) {
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}
'''
normalize_with_helper = normalize_marker + '''
static bool minic_riscv64_emit_integer_result_conversion(FILE *file,
                                                         MinicType operation_type,
                                                         MinicType result_type,
                                                         const char *register_name) {
    if (!minic_riscv64_emit_integer_conversion(file, operation_type, register_name)) {
        return false;
    }
    return minic_type_equal(operation_type, result_type) ||
           minic_riscv64_emit_integer_conversion(file, result_type, register_name);
}
'''
if text.count(normalize_marker) != 1:
    raise SystemExit('expression helper marker mismatch')
text = text.replace(normalize_marker, normalize_with_helper, 1)

replacements = [
    (
        '            return fprintf(file, "  negw a0, a0\\n") >= 0 &&\n',
        '            return fprintf(file,\n'
        '                           minic_type_is_long_integer(expression->type)\n'
        '                               ? "  neg a0, a0\\n"\n'
        '                               : "  negw a0, a0\\n") >= 0 &&\n',
    ),
    (
        '''                return fprintf(file, "  addw a0, t0, a0\\n") >= 0 &&
                       minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''                return fprintf(file,
                               minic_type_is_long_integer(common_integer_type)
                                   ? "  add a0, t0, a0\\n"
                                   : "  addw a0, t0, a0\\n") >= 0 &&
                       minic_riscv64_emit_integer_result_conversion(
                           file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''                return fprintf(file, "  subw a0, t0, a0\\n") >= 0 &&
                       minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''                return fprintf(file,
                               minic_type_is_long_integer(common_integer_type)
                                   ? "  sub a0, t0, a0\\n"
                                   : "  subw a0, t0, a0\\n") >= 0 &&
                       minic_riscv64_emit_integer_result_conversion(
                           file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''            return has_integer_common_type && fprintf(file, "  mulw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(common_integer_type)
                               ? "  mul a0, t0, a0\\n"
                               : "  mulw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  divuw a0, t0, a0\\n"
                               : "  divw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''                           minic_type_is_long_integer(common_integer_type)
                               ? (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  divu a0, t0, a0\\n"
                                      : "  div a0, t0, a0\\n")
                               : (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  divuw a0, t0, a0\\n"
                                      : "  divw a0, t0, a0\\n")) >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  remuw a0, t0, a0\\n"
                               : "  remw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''                           minic_type_is_long_integer(common_integer_type)
                               ? (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  remu a0, t0, a0\\n"
                                      : "  rem a0, t0, a0\\n")
                               : (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  remuw a0, t0, a0\\n"
                                      : "  remw a0, t0, a0\\n")) >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''            return has_integer_common_type && fprintf(file, "  sllw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(expression->type)
                               ? "  sll a0, t0, a0\\n"
                               : "  sllw a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
    ),
    (
        '''                           minic_type_is_unsigned_integer(expression->type)
                               ? "  srlw a0, t0, a0\\n"
                               : "  sraw a0, t0, a0\\n") >= 0 &&
''',
        '''                           minic_type_is_long_integer(expression->type)
                               ? (minic_type_is_unsigned_integer(expression->type)
                                      ? "  srl a0, t0, a0\\n"
                                      : "  sra a0, t0, a0\\n")
                               : (minic_type_is_unsigned_integer(expression->type)
                                      ? "  srlw a0, t0, a0\\n"
                                      : "  sraw a0, t0, a0\\n")) >= 0 &&
''',
    ),
    (
        '''            return has_integer_common_type && fprintf(file, "  and a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''            return has_integer_common_type && fprintf(file, "  and a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
''',
    ),
    (
        '''            return has_integer_common_type && fprintf(file, "  xor a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
        '''            return has_integer_common_type && fprintf(file, "  xor a0, t0, a0\\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
''',
    ),
]
for old, new in replacements:
    if text.count(old) != 1:
        raise SystemExit(f'expression replacement marker count={text.count(old)} for {old[:60]!r}')
    text = text.replace(old, new, 1)
expression.write_text(text)

program = Path('tests/programs/c0/long_integer_semantics.c')
program.write_text('''static const unsigned long global_words[2] = {1, 2};

static unsigned long pass_unsigned(unsigned long value)
{
    return value;
}

static long pass_signed(long value)
{
    return value;
}

int main(void)
{
    unsigned long one = global_words[0];
    unsigned long high = one << 63;
    unsigned long wide = one << 40;
    unsigned long value = pass_unsigned(high + wide + 37);
    unsigned long product = (wide + 3) * 5;
    long negative = -((long)one << 40);

    if (value < high) {
        return 1;
    }
    if ((value >> 63) != 1) {
        return 2;
    }
    if ((value - high) != wide + 37) {
        return 3;
    }
    if (product / 5 != wide + 3) {
        return 4;
    }
    if (product % 5 != 0) {
        return 5;
    }
    if ((high ^ high) != 0) {
        return 6;
    }
    if (pass_signed(negative) >= 0) {
        return 7;
    }
    if ((negative >> 39) != -2) {
        return 8;
    }
    if ((unsigned long)negative == 0) {
        return 9;
    }
    if (global_words[1] != 2) {
        return 10;
    }
    return 0;
}
''')
manifest = Path('tests/programs/c0/manifest.txt')
text = manifest.read_text()
if 'long_integer_semantics\n' in text:
    raise SystemExit('long program already registered')
manifest.write_text(text + 'long_integer_semantics\n')
PY

clang-format-18 -i \
  src/target/riscv64/codegen_support.c \
  src/target/riscv64/codegen_expression.c \
  src/target/riscv64/codegen_function.c

make -j2 check-fast

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  gcc-riscv64-linux-gnu libc6-dev-riscv64-cross qemu-user
make -j2 MODE=release BUILD_DIR=build/long-codegen
MINIC="$PWD/build/long-codegen/bin/minic" \
BUILD_DIR="$PWD/build/long-codegen" \
RISCV_CC=riscv64-linux-gnu-gcc \
RISCV_OBJDUMP=riscv64-linux-gnu-objdump \
QEMU_RISCV64=qemu-riscv64 \
REQUIRE_RISCV_RUNTIME=1 \
  sh tests/programs/c0/run.sh

grep -F '.dword 1' build/long-codegen/tests/programs-c0/long_integer_semantics.minic.s >/dev/null
grep -F '  ld a0,' build/long-codegen/tests/programs-c0/long_integer_semantics.minic.s >/dev/null
grep -F '  sd ' build/long-codegen/tests/programs-c0/long_integer_semantics.minic.s >/dev/null
grep -F '  sll ' build/long-codegen/tests/programs-c0/long_integer_semantics.minic.s >/dev/null
grep -F '  divu ' build/long-codegen/tests/programs-c0/long_integer_semantics.minic.s >/dev/null

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git rm \
  .github/scripts/apply-rv64-long-codegen.sh \
  .github/workflows/apply-rv64-long-codegen.yml
git add src tests
git commit -m "riscv: lower long integers at full RV64 width" -m "Use eight-byte LONG loads, stores, globals, conversions, arithmetic, shifts, signed and unsigned division/remainder, calls, and returns. Add the fortieth GCC/MiniC differential program to exercise high-bit values across memory and function boundaries.\n\n中文说明：为 LONG 使用八字节加载、存储、全局对象、转换、算术、移位、有符号/无符号除余、调用和返回；增加第 40 个 GCC/MiniC 差分程序，覆盖高位值跨内存与函数边界的行为。\n\nValidation / 验证： complete host fast gate and 40 RV64/QEMU differential programs PASS; .dword, ld/sd, full-width shift and divu assembly evidence PASS."
git push origin HEAD:frontend/rv64-long-integers

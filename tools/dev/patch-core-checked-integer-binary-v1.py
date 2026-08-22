from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# Core IR: the second real checked-overflow consumer justifies one small operation enum.
replace_once(
    "src/core/core_ir.h",
    "typedef enum MinicCoreInstructionKind {\n",
    "typedef enum MinicCoreIntegerOverflowOperator {\n"
    "    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,\n"
    "    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY\n"
    "} MinicCoreIntegerOverflowOperator;\n\n"
    "typedef enum MinicCoreInstructionKind {\n",
)
replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW,",
    "    MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW,",
)
replace_once(
    "src/core/core_ir.h",
    "        struct {\n"
    "            MinicCoreValueId left;\n"
    "            MinicCoreValueId right;\n"
    "            MinicCoreValueId result_address;\n"
    "        } multiply_overflow;",
    "        struct {\n"
    "            MinicCoreIntegerOverflowOperator operator_kind;\n"
    "            MinicCoreValueId left;\n"
    "            MinicCoreValueId right;\n"
    "            MinicCoreValueId result_address;\n"
    "        } integer_overflow;",
)

# Verifier + dump: generic checked integer binary, still only operations with real consumers.
replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW: {\n"
    "        MinicType result_type;\n\n"
    "        if (!instruction_result_is_valid(function, instruction) ||\n"
    "            !minic_type_equal(instruction->type, minic_type_bool()) ||\n"
    "            instruction->value.multiply_overflow.left >= function->value_count ||\n"
    "            instruction->value.multiply_overflow.right >= function->value_count ||\n"
    "            instruction->value.multiply_overflow.result_address >= function->value_count ||\n"
    "            !available_values[instruction->value.multiply_overflow.left] ||\n"
    "            !available_values[instruction->value.multiply_overflow.right] ||\n"
    "            !available_values[instruction->value.multiply_overflow.result_address] ||\n"
    "            !minic_type_pointee(\n"
    "                function->values[instruction->value.multiply_overflow.result_address].type,\n"
    "                &result_type) ||\n"
    "            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n"
    "            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {\n"
    "            return false;\n"
    "        }\n"
    "        left = &function->values[instruction->value.multiply_overflow.left];\n"
    "        right = &function->values[instruction->value.multiply_overflow.right];\n"
    "        return minic_type_equal(left->type, result_type) &&\n"
    "               minic_type_equal(right->type, result_type);\n"
    "    }",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n"
    "        MinicType result_type;\n\n"
    "        if (!instruction_result_is_valid(function, instruction) ||\n"
    "            !minic_type_equal(instruction->type, minic_type_bool()) ||\n"
    "            (instruction->value.integer_overflow.operator_kind !=\n"
    "                 MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "             instruction->value.integer_overflow.operator_kind !=\n"
    "                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||\n"
    "            instruction->value.integer_overflow.left >= function->value_count ||\n"
    "            instruction->value.integer_overflow.right >= function->value_count ||\n"
    "            instruction->value.integer_overflow.result_address >= function->value_count ||\n"
    "            !available_values[instruction->value.integer_overflow.left] ||\n"
    "            !available_values[instruction->value.integer_overflow.right] ||\n"
    "            !available_values[instruction->value.integer_overflow.result_address] ||\n"
    "            !minic_type_pointee(\n"
    "                function->values[instruction->value.integer_overflow.result_address].type,\n"
    "                &result_type) ||\n"
    "            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||\n"
    "            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {\n"
    "            return false;\n"
    "        }\n"
    "        left = &function->values[instruction->value.integer_overflow.left];\n"
    "        right = &function->values[instruction->value.integer_overflow.right];\n"
    "        return minic_type_equal(left->type, result_type) &&\n"
    "               minic_type_equal(right->type, result_type);\n"
    "    }",
)
replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = mul.overflow.int %%%\" PRIu32 \", %%%\" PRIu32 \", %%%\" PRIu32\n"
    "                       \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.multiply_overflow.left,\n"
    "                       instruction->value.multiply_overflow.right,\n"
    "                       instruction->value.multiply_overflow.result_address) >= 0;",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {\n"
    "        const char *operator_name =\n"
    "            instruction->value.integer_overflow.operator_kind ==\n"
    "                    MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                ? \"add\"\n"
    "                : \"mul\";\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = %s.overflow.int %%%\" PRIu32 \", %%%\" PRIu32 \", %%%\" PRIu32\n"
    "                       \"\\n\",\n"
    "                       instruction->result,\n"
    "                       operator_name,\n"
    "                       instruction->value.integer_overflow.left,\n"
    "                       instruction->value.integer_overflow.right,\n"
    "                       instruction->value.integer_overflow.result_address) >= 0;\n"
    "    }",
)

# AST -> Core: share all operand/result-address semantics; select only ADD or MULTIPLY.
replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&\n"
    "        expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY) {",
    "    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&\n"
    "        (expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD ||\n"
    "         expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY)) {",
)
text = Path("src/core/core_lower.c").read_text()
text = text.replace("MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW", "MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW")
text = text.replace("instruction.value.multiply_overflow.", "instruction.value.integer_overflow.")
anchor = "        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;\n        instruction.type = minic_type_bool();\n"
if text.count(anchor) != 1:
    raise SystemExit("expected one Core overflow instruction anchor")
text = text.replace(
    anchor,
    "        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;\n"
    "        instruction.type = minic_type_bool();\n"
    "        instruction.value.integer_overflow.operator_kind =\n"
    "            expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD\n"
    "                ? MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                : MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;\n",
    1,
)
Path("src/core/core_lower.c").write_text(text)

# RV64: generic checked integer binary support and emission.
text = Path("src/target/riscv64/core_codegen.c").read_text()
text = text.replace("core_integer_multiply_overflow_supported", "core_integer_overflow_supported")
text = text.replace("MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW", "MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW")
text = text.replace("instruction->value.multiply_overflow.", "instruction->value.integer_overflow.")
Path("src/target/riscv64/core_codegen.c").write_text(text)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||\n"
    "        !minic_type_equal(instruction->type, minic_type_bool()) ||",
    "        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||\n"
    "        !minic_type_equal(instruction->type, minic_type_bool()) ||\n"
    "        (instruction->value.integer_overflow.operator_kind !=\n"
    "             MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "         instruction->value.integer_overflow.operator_kind !=\n"
    "             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||",
)

old_emit = """    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        if (!core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !load_core_value(file, frame, instruction->value.integer_overflow.left, "t0") ||
            !load_core_value(file, frame, instruction->value.integer_overflow.right, "t1") ||
            !load_core_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3")) {
            return false;
        }
        if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                fprintf(file, "  xor t4, t4, t2\\n  snez t4, t4\\n") < 0) {
                return false;
            }
        } else if (is_unsigned) {
            if (fprintf(file,
                        "  mul t2, t0, t1\\n"
                        "  mulhu t4, t0, t1\\n"
                        "  snez t4, t4\\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  mul t2, t0, t1\\n"
                           "  mulh t4, t0, t1\\n"
                           "  srai t5, t2, 63\\n"
                           "  xor t4, t4, t5\\n"
                           "  snez t4, t4\\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_scalar_store_for_program(file, program, result_type, "t2", "t3")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t4");
    }
"""
new_emit = """    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        if (!core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !load_core_value(file, frame, instruction->value.integer_overflow.left, "t0") ||
            !load_core_value(file, frame, instruction->value.integer_overflow.right, "t1") ||
            !load_core_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3")) {
            return false;
        }
        if (instruction->value.integer_overflow.operator_kind ==
            MINIC_CORE_INTEGER_OVERFLOW_ADD) {
            if (result_size < 8U) {
                if (fprintf(file, "  add t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\\n  snez t4, t4\\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  add t2, t0, t1\\n"
                            "  sltu t4, t2, t0\\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  add t2, t0, t1\\n"
                               "  xor t4, t0, t1\\n"
                               "  xori t4, t4, -1\\n"
                               "  xor t5, t0, t2\\n"
                               "  and t4, t4, t5\\n"
                               "  srli t4, t4, 63\\n") < 0) {
                return false;
            }
        } else if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                fprintf(file, "  xor t4, t4, t2\\n  snez t4, t4\\n") < 0) {
                return false;
            }
        } else if (is_unsigned) {
            if (fprintf(file,
                        "  mul t2, t0, t1\\n"
                        "  mulhu t4, t0, t1\\n"
                        "  snez t4, t4\\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  mul t2, t0, t1\\n"
                           "  mulh t4, t0, t1\\n"
                           "  srai t5, t2, 63\\n"
                           "  xor t4, t4, t5\\n"
                           "  snez t4, t4\\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_scalar_store_for_program(file, program, result_type, "t2", "t3")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t4");
    }
"""
replace_once("src/target/riscv64/core_codegen.c", old_emit, new_emit)

# Focused ADD-overflow contract. MUL's permanent M6 test remains and is rerun by the one-shot.
Path("tests/compiler/c0/core_integer_add_overflow.c").write_text("""int core_m8_checked_int(int left, int right, int *result) {
    return __builtin_add_overflow(left, right, result);
}

long core_m8_checked_long(long left, long right, long *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned int core_m8_checked_uint(unsigned int left, unsigned int right, unsigned int *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned long
core_m8_checked_ulong(unsigned long left, unsigned long right, unsigned long *result) {
    return __builtin_add_overflow(left, right, result);
}

unsigned long core_m8_size_add(unsigned long left, unsigned long right) {
    unsigned long bytes;
    if (__builtin_add_overflow(left, right, &bytes))
        return ~0UL;
    return bytes;
}
""")
Path("tests/compiler/c0/core_integer_add_overflow_runtime.c").write_text("""#include <limits.h>
#include <stdio.h>

int core_m8_checked_int(int left, int right, int *result);
long core_m8_checked_long(long left, long right, long *result);
unsigned int core_m8_checked_uint(unsigned int left, unsigned int right, unsigned int *result);
unsigned long core_m8_checked_ulong(unsigned long left, unsigned long right, unsigned long *result);
unsigned long core_m8_size_add(unsigned long left, unsigned long right);

int main(void) {
    int si = 0;
    long sl = 0;
    unsigned int ui = 0;
    unsigned long ul = 0;
    int oi = core_m8_checked_int(INT_MAX, 1, &si);
    int ol = core_m8_checked_long(LONG_MIN, -1L, &sl);
    int oui = core_m8_checked_uint(UINT_MAX, 1U, &ui);
    int oul = core_m8_checked_ulong(ULONG_MAX, 1UL, &ul);
    printf("%d %d %d %u %d %ld %d %lu %lu %lu\\n",
           oi,
           si,
           oui,
           ui,
           ol,
           sl,
           oul,
           ul,
           core_m8_size_add(ULONG_MAX, 1UL),
           core_m8_size_add(40UL, 2UL));
    return 0;
}
""")
Path("tests/compiler/c0/run-core-integer-add-overflow.sh").write_text("""#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-add-overflow}"
source_file="$root/tests/compiler/c0/core_integer_add_overflow.c"
runtime_file="$root/tests/compiler/c0/core_integer_add_overflow_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_add_overflow.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_add_overflow.i" \
    -o "$work/core_integer_add_overflow-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_add_overflow.i" \
    -o "$work/core_integer_add_overflow-core.s"

grep -q '^core_m8_checked_int:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_long:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_uint:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_ulong:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_size_add:' "$work/core_integer_add_overflow-core.s"
grep -q 'sltu t4, t2, t0' "$work/core_integer_add_overflow-core.s"
grep -q 'srli t4, t4, 63' "$work/core_integer_add_overflow-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_add_overflow-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-integer-add-overflow'
""")

# Permanent C0 gate registration.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_integer_multiply_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-multiply-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh\n"
    "}\n\n"
    "core_integer_bitwise_not_focused() {",
    "core_integer_multiply_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-multiply-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh\n"
    "}\n\n"
    "core_integer_add_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-add-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-add-overflow.sh\n"
    "}\n\n"
    "core_integer_bitwise_not_focused() {",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
    "start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused\n"
    "start_gate core-integer-add-overflow-focused core_integer_add_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
)

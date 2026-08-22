#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


compiler_path = Path("src/compiler/compiler.c")
compiler = compiler_path.read_text()
compiler = replace_once(
    compiler,
    "typedef struct MinicCoreCandidates {\n    MinicCoreFunction *functions;\n    MinicCoreLowerStatus *statuses;\n    size_t function_count;\n} MinicCoreCandidates;",
    "typedef struct MinicCoreCandidates {\n    MinicCoreFunction *functions;\n    MinicCoreLowerStatus *statuses;\n    bool *core_required;\n    size_t function_count;\n} MinicCoreCandidates;",
    "candidate struct",
)
compiler = replace_once(
    compiler,
    "    candidates->functions = NULL;\n    candidates->statuses = NULL;\n    candidates->function_count = 0U;",
    "    candidates->functions = NULL;\n    candidates->statuses = NULL;\n    candidates->core_required = NULL;\n    candidates->function_count = 0U;",
    "candidate initialize",
)
compiler = replace_once(
    compiler,
    "    free(candidates->functions);\n    free(candidates->statuses);\n    minic_core_candidates_initialize(candidates);",
    "    free(candidates->functions);\n    free(candidates->statuses);\n    free(candidates->core_required);\n    minic_core_candidates_initialize(candidates);",
    "candidate destroy",
)
compiler = replace_once(
    compiler,
    "        candidates.functions =\n            (MinicCoreFunction *)calloc(candidates.function_count, sizeof(*candidates.functions));\n        candidates.statuses = (MinicCoreLowerStatus *)malloc(candidates.function_count *\n                                                             sizeof(*candidates.statuses));\n        if (candidates.functions == NULL || candidates.statuses == NULL) {\n            free(candidates.functions);\n            free(candidates.statuses);\n            return false;\n        }",
    "        candidates.functions =\n            (MinicCoreFunction *)calloc(candidates.function_count, sizeof(*candidates.functions));\n        candidates.statuses = (MinicCoreLowerStatus *)malloc(candidates.function_count *\n                                                             sizeof(*candidates.statuses));\n        candidates.core_required =\n            (bool *)calloc(candidates.function_count, sizeof(*candidates.core_required));\n        if (candidates.functions == NULL || candidates.statuses == NULL ||\n            candidates.core_required == NULL) {\n            free(candidates.functions);\n            free(candidates.statuses);\n            free(candidates.core_required);\n            return false;\n        }",
    "candidate allocation",
)
compiler = replace_once(
    compiler,
    "        candidates.statuses[function_index] =\n            minic_core_lower_function(&body, &candidates.functions[function_index]);",
    "        candidates.statuses[function_index] =\n            minic_core_lower_function(&body, &candidates.functions[function_index]);\n        candidates.core_required[function_index] =\n            candidates.statuses[function_index] == MINIC_CORE_LOWER_OK;",
    "candidate ownership",
)
compiler = replace_once(
    compiler,
    "         (candidates->functions == NULL || candidates->statuses == NULL))) {",
    "         (candidates->functions == NULL || candidates->statuses == NULL ||\n          candidates->core_required == NULL))) {",
    "candidate validation",
)
compiler = replace_once(
    compiler,
    "                                                                core_candidates.functions,\n                                                                core_candidates.function_count,\n                                                                diagnostic);",
    "                                                                core_candidates.functions,\n                                                                core_candidates.core_required,\n                                                                core_candidates.function_count,\n                                                                diagnostic);",
    "compiler writer call",
)
compiler_path.write_text(compiler)

header_path = Path("src/target/riscv64/codegen.h")
header = header_path.read_text()
header = replace_once(
    header,
    "                                                         const MinicCoreFunction *core_functions,\n                                                         size_t core_function_count,\n                                                         MinicDiagnostic *diagnostic);",
    "                                                         const MinicCoreFunction *core_functions,\n                                                         const bool *core_required_functions,\n                                                         size_t core_function_count,\n                                                         MinicDiagnostic *diagnostic);",
    "codegen header signature",
)
header_path.write_text(header)

codegen_path = Path("src/target/riscv64/codegen_function.c")
codegen = codegen_path.read_text()
codegen = replace_once(
    codegen,
    "bool minic_riscv64_write_c0_program_with_core_candidates(const char *path,\n                                                         const MinicC0Program *program,\n                                                         const MinicCoreFunction *core_functions,\n                                                         size_t core_function_count,\n                                                         MinicDiagnostic *diagnostic) {",
    "bool minic_riscv64_write_c0_program_with_core_candidates(const char *path,\n                                                         const MinicC0Program *program,\n                                                         const MinicCoreFunction *core_functions,\n                                                         const bool *core_required_functions,\n                                                         size_t core_function_count,\n                                                         MinicDiagnostic *diagnostic) {",
    "codegen definition signature",
)
codegen = replace_once(
    codegen,
    "    if ((core_functions == NULL && core_function_count != 0U) ||\n        (core_functions != NULL && core_function_count != program->function_count)) {",
    "    if ((core_functions == NULL) != (core_required_functions == NULL) ||\n        (core_functions == NULL && core_function_count != 0U) ||\n        (core_functions != NULL && core_function_count != program->function_count)) {",
    "codegen candidate map validation",
)
codegen = replace_once(
    codegen,
    "        const MinicFunction *function;\n        const MinicCoreFunction *core_function;",
    "        const MinicFunction *function;\n        const MinicCoreFunction *core_function;\n        bool core_required;",
    "codegen loop locals",
)
old_route = '''        core_function = core_functions != NULL ? &core_functions[function_index] : NULL;
        if (core_function != NULL &&
            minic_riscv64_core_function_can_emit_basic_v0_for_program(program, core_function)) {
            MinicRiscv64FunctionSymbol symbol;

            success = minic_riscv64_function_symbol_from_function(function, &symbol) &&
                      minic_riscv64_emit_core_function_basic_v0_for_program_with_symbol(
                          file, program, core_function, &symbol);
        } else {
            const char *failure_stage;
'''
new_route = '''        core_required =
            core_required_functions != NULL && core_required_functions[function_index];
        core_function = core_functions != NULL ? &core_functions[function_index] : NULL;
        if (core_required) {
            MinicRiscv64FunctionSymbol symbol;

            if (core_function == NULL ||
                !minic_riscv64_core_function_can_emit_basic_v0_for_program(program, core_function)) {
                char message[256];
                const char *symbol_name;

                symbol_name = minic_c0_function_symbol_name(function);
                (void)snprintf(message,
                               sizeof(message),
                               "Core-owned function '%s' cannot be emitted by RV64 basic-v0",
                               symbol_name != NULL ? symbol_name : "<unnamed>");
                minic_riscv64_set_diagnostic(diagnostic, path, message);
                success = false;
                continue;
            }
            success = minic_riscv64_function_symbol_from_function(function, &symbol) &&
                      minic_riscv64_emit_core_function_basic_v0_for_program_with_symbol(
                          file, program, core_function, &symbol);
        } else {
            const char *failure_stage;
'''
codegen = replace_once(codegen, old_route, new_route, "codegen Core route")
codegen = replace_once(
    codegen,
    "    return minic_riscv64_write_c0_program_with_core_candidates(path, program, NULL, 0U, diagnostic);",
    "    return minic_riscv64_write_c0_program_with_core_candidates(\n        path, program, NULL, NULL, 0U, diagnostic);",
    "legacy writer wrapper",
)
codegen_path.write_text(codegen)

source_path = Path("tests/compiler/c0/core_required_no_fallback.c")
source_path.write_text(
    '''static int core_owned_sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {\n'''
    '''    return a + b + c + d + e + f + g + h + i;\n'''
    '''}\n\n'''
    '''int core_owned_call9(void) {\n'''
    '''    return core_owned_sum9(1, 2, 3, 4, 5, 6, 7, 8, 9);\n'''
    '''}\n'''
)

script_path = Path("tests/compiler/c0/run-core-required-no-fallback.sh")
script_path.write_text(
    '''#!/bin/sh\n'''
    '''set -eu\n\n'''
    '''root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\n'''
    '''minic=${MINIC:-"$root/build/debug/bin/minic"}\n'''
    '''host_cc=${HOST_CC:-${CC:-cc}}\n'''
    '''work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-core-required-no-fallback\n\n'''
    '''mkdir -p "$work"\n'''
    '''"$host_cc" -E -P -std=gnu11 -x c \\\n'''
    '''    "$root/tests/compiler/c0/core_required_no_fallback.c" \\\n'''
    '''    -o "$work/core_required_no_fallback.i"\n\n'''
    '''"$minic" -S "$work/core_required_no_fallback.i" -o "$work/legacy.s"\n'''
    '''MINIC_CORE_IR=strict "$minic" -S "$work/core_required_no_fallback.i" \\\n'''
    '''    -o "$work/shadow-strict.s"\n'''
    '''if MINIC_CORE_CODEGEN=basic-v0 "$minic" -S "$work/core_required_no_fallback.i" \\\n'''
    '''    -o "$work/core-basic-v0.s" >"$work/core.stdout" 2>"$work/core.stderr"; then\n'''
    '''    printf '%s\\n' 'FAIL compiler/c0/core-required-no-fallback: Core-owned target gap silently fell back' >&2\n'''
    '''    exit 1\n'''
    '''fi\n'''
    '''grep -F "Core-owned function 'core_owned_call9' cannot be emitted by RV64 basic-v0" \\\n'''
    '''    "$work/core.stderr" >/dev/null\n'''
    '''printf '%s\\n' 'PASS compiler/c0/core-required-no-fallback lower-ok=1 target-gap=fail-closed legacy-default=1'\n'''
)
script_path.chmod(0o755)

gate_path = Path(".github/scripts/compiler-c0-full-gate.sh")
gate = gate_path.read_text()
gate = replace_once(
    gate,
    '''runtime_record_array_initializer_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-runtime-record-array-initializer" \\
        sh tests/compiler/c0/run-runtime-record-array-initializers.sh
}

''',
    '''runtime_record_array_initializer_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-runtime-record-array-initializer" \\
        sh tests/compiler/c0/run-runtime-record-array-initializers.sh
}

core_required_no_fallback_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    HOST_CC=cc \\
    BUILD_DIR="$root/build/ci-core-required-no-fallback" \\
        sh tests/compiler/c0/run-core-required-no-fallback.sh
}

''',
    "C0 helper",
)
gate = replace_once(
    gate,
    "start_gate record-array-init-focused runtime_record_array_initializer_focused\n",
    "start_gate record-array-init-focused runtime_record_array_initializer_focused\nstart_gate core-required-no-fallback-focused core_required_no_fallback_focused\n",
    "C0 gate registration",
)
gate_path.write_text(gate)

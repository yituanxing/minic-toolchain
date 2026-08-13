from pathlib import Path

# Program-owned translation-unit raw assembly entity.  It is intentionally
# separate from MinicInlineAsm, whose operands/labels are function-owned.
ast_h = Path("src/frontend/ast.h")
text = ast_h.read_text()
old = '''typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    MinicInlineAsmOperand *outputs;
'''
new = '''typedef struct MinicFileAsm {
    char *text;
    size_t length;
} MinicFileAsm;

typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    MinicInlineAsmOperand *outputs;
'''
if text.count(old) != 1:
    raise SystemExit(f"file-asm type anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    MinicInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;

    MinicBlock *blocks;
'''
new = '''    MinicInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;

    MinicFileAsm *file_asms;
    size_t file_asm_count;
    size_t file_asm_capacity;

    MinicBlock *blocks;
'''
if text.count(old) != 1:
    raise SystemExit(f"program file-asm storage anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);
bool minic_c0_program_add_inline_asm(MinicC0Program *program,
'''
new = '''bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);
bool minic_c0_program_add_file_asm(MinicC0Program *program, const char *text, size_t length);
bool minic_c0_program_add_inline_asm(MinicC0Program *program,
'''
if text.count(old) != 1:
    raise SystemExit(f"file-asm API anchor mismatch: {text.count(old)}")
ast_h.write_text(text.replace(old, new, 1))

# Entity ownership/lifetime.
ast_c = Path("src/frontend/ast.c")
text = ast_c.read_text()
old = '''    for (index = 0U; index < program->inline_asm_count; ++index) {
        size_t clobber_index;
'''
new = '''    for (index = 0U; index < program->file_asm_count; ++index) {
        free(program->file_asms[index].text);
    }
    for (index = 0U; index < program->inline_asm_count; ++index) {
        size_t clobber_index;
'''
if text.count(old) != 1:
    raise SystemExit(f"file-asm destroy-loop anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''    free(program->statements);
    free(program->inline_asms);
    free(program->blocks);
'''
new = '''    free(program->statements);
    free(program->inline_asms);
    free(program->file_asms);
    free(program->blocks);
'''
if text.count(old) != 1:
    raise SystemExit(f"file-asm destroy-storage anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
insert_at = text.find("bool minic_c0_program_add_inline_asm(")
if insert_at < 0:
    raise SystemExit("inline asm owner missing")
helper = '''bool minic_c0_program_add_file_asm(MinicC0Program *program, const char *text, size_t length) {
    MinicFileAsm file_asm;

    if (program == NULL || text == NULL || length == SIZE_MAX ||
        memchr(text, '\\0', length) != NULL ||
        !minic_grow_array((void **)&program->file_asms,
                          &program->file_asm_capacity,
                          program->file_asm_count,
                          sizeof(*program->file_asms))) {
        return false;
    }
    (void)memset(&file_asm, 0, sizeof(file_asm));
    file_asm.text = minic_copy_name(text, length);
    if (file_asm.text == NULL) {
        return false;
    }
    file_asm.length = length;
    program->file_asms[program->file_asm_count] = file_asm;
    program->file_asm_count += 1U;
    return true;
}

'''
if "minic_c0_program_add_file_asm" in text:
    raise SystemExit("file asm API already materialized")
text = text[:insert_at] + helper + text[insert_at:]
ast_c.write_text(text)

# Verifier freezes owned storage and the NUL-free/null-terminated invariant used
# by the textual backend while allowing the standard empty basic-asm string.
verifier = Path("src/frontend/ast_verifier.c")
text = verifier.read_text()
old = '''    for (index = 0U; index < program->expression_count; ++index) {
        if (!verify_expression(program, index, form, target)) {
'''
new = '''    if (!storage_is_valid(program->file_asms, program->file_asm_count, program->file_asm_capacity)) {
        return false;
    }
    for (index = 0U; index < program->file_asm_count; ++index) {
        const MinicFileAsm *file_asm;

        file_asm = &program->file_asms[index];
        if (file_asm->text == NULL || strlen(file_asm->text) != file_asm->length) {
            return false;
        }
    }
    for (index = 0U; index < program->expression_count; ++index) {
        if (!verify_expression(program, index, form, target)) {
'''
if text.count(old) != 1:
    raise SystemExit(f"file-asm verifier anchor mismatch: {text.count(old)}")
verifier.write_text(text.replace(old, new, 1))

# File-scope basic asm parser: no qualifiers/operands.  The shared string-text
# decoder already owns C escape processing and adjacent literal concatenation.
parser = Path("src/frontend/parser_function.c")
text = parser.read_text()
insert_at = text.find("static bool top_level_is_gnu_extension_marker(")
if insert_at < 0:
    raise SystemExit("top-level extension marker owner missing")
helper = '''static bool top_level_is_gnu_asm(const MinicParser *parser) {
    return function_identifier_is(parser, "asm") || function_identifier_is(parser, "__asm") ||
           function_identifier_is(parser, "__asm__");
}

static bool parse_top_level_gnu_basic_asm(MinicParser *parser) {
    char *assembly_text;
    size_t assembly_length;
    MinicSourceSpan assembly_span;

    if (parser == NULL || !top_level_is_gnu_asm(parser)) {
        return false;
    }
    assembly_text = NULL;
    assembly_length = 0U;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        minic_parser_error(parser, "file-scope GNU basic asm does not allow qualifiers");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_string_text(
            parser, &assembly_text, &assembly_length, &assembly_span)) {
        free(assembly_text);
        return false;
    }
    (void)assembly_span;
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        free(assembly_text);
        minic_parser_error(parser, "file-scope GNU basic asm does not support operands");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after file-scope GNU asm") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after file-scope GNU asm") ||
        !minic_c0_program_add_file_asm(parser->program, assembly_text, assembly_length)) {
        free(assembly_text);
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser, "cannot store file-scope GNU basic asm");
        }
        return false;
    }
    free(assembly_text);
    return true;
}

'''
if "parse_top_level_gnu_basic_asm" in text:
    raise SystemExit("top-level basic asm already materialized")
text = text[:insert_at] + helper + text[insert_at:]
old = '''        if (parser.current.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
            success = parse_top_level_preprocessor_directive(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {
'''
new = '''        if (parser.current.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
            success = parse_top_level_preprocessor_directive(&parser);
        } else if (top_level_is_gnu_asm(&parser)) {
            success = parse_top_level_gnu_basic_asm(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {
'''
if text.count(old) != 1:
    raise SystemExit(f"top-level basic asm dispatch anchor mismatch: {text.count(old)}")
parser.write_text(text.replace(old, new, 1))

# RV64 writer: basic asm is raw assembler text, not inline-asm template syntax.
# Emit all file-scope basic asm after globals and before functions, preserving
# their own source order.  Force .text before and after so C emission cannot
# inherit a section selected by either the preceding global or a raw asm block.
codegen = Path("src/target/riscv64/codegen_function.c")
text = codegen.read_text()
insert_at = text.find("static bool minic_riscv64_emit_global_object(")
if insert_at < 0:
    raise SystemExit("global object emitter owner missing")
helper = '''static bool minic_riscv64_emit_file_asm(FILE *file, const MinicFileAsm *file_asm) {
    if (file == NULL || file_asm == NULL || file_asm->text == NULL) {
        return false;
    }
    if (file_asm->length != 0U &&
        fwrite(file_asm->text, 1U, file_asm->length, file) != file_asm->length) {
        return false;
    }
    return fputc('\\n', file) != EOF;
}

'''
if "minic_riscv64_emit_file_asm" in text:
    raise SystemExit("file asm emitter already materialized")
text = text[:insert_at] + helper + text[insert_at:]
old = '''    if (success) {
        success = fprintf(file, ".text\\n") >= 0;
    }

    label_counter = 0U;
'''
new = '''    if (success && program->file_asm_count != 0U) {
        size_t file_asm_index;

        success = fprintf(file, ".text\\n") >= 0;
        for (file_asm_index = 0U; success && file_asm_index < program->file_asm_count;
             ++file_asm_index) {
            success = minic_riscv64_emit_file_asm(file, &program->file_asms[file_asm_index]);
        }
    }
    if (success) {
        success = fprintf(file, ".text\\n") >= 0;
    }

    label_counter = 0U;
'''
if text.count(old) != 1:
    raise SystemExit(f"file asm writer ordering anchor mismatch: {text.count(old)}")
codegen.write_text(text.replace(old, new, 1))

# Focused source uses the exact Linux-tail assembler-directive shape plus
# aliases, adjacent C strings and a literal percent sign (basic asm must not
# interpret extended-asm percent operands).
Path("tests/compiler/c0/file_scope_basic_asm.c").write_text(r'''int file_asm_target;

asm(".section \\".export_symbol\\",\\"a\\" ; __export_symbol_file_asm_target: ; "
    ".asciz \\"\\" ; .ascii \\"\\" \\"\\\\0\\" ; .balign 8 ; .quad file_asm_target ; .previous");

__asm__(".section \\".minic.fileasm\\",\\"a\\" ; __minic_file_asm_second: ; "
        ".ascii \\"%\\" ; .previous");

__asm(".section \\".minic.fileasm\\",\\"a\\" ; __minic_file_asm_third: ; .previous");
''')
Path("tests/compiler/c0/invalid_file_scope_basic_asm_qualifier.c").write_text(
    'asm volatile("nop");\n'
)
Path("tests/compiler/c0/invalid_file_scope_basic_asm_operands.c").write_text(
    'int value; asm(".quad %0" : : "r"(value));\n'
)
Path("tests/compiler/c0/run-file-scope-basic-asm.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-file-scope-basic-asm
host_cc=${HOST_CC:-cc}

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/file_scope_basic_asm.c" -o "$work/file_scope_basic_asm.s"
test -s "$work/file_scope_basic_asm.s"
grep -F '.section ".export_symbol","a" ; __export_symbol_file_asm_target:' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.ascii "" "\0"' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.quad file_asm_target' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.ascii "%"' "$work/file_scope_basic_asm.s" >/dev/null
first=$(grep -n -F '__export_symbol_file_asm_target:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
second=$(grep -n -F '__minic_file_asm_second:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
third=$(grep -n -F '__minic_file_asm_third:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
test "$first" -lt "$second"
test "$second" -lt "$third"
"$host_cc" -c "$work/file_scope_basic_asm.s" -o "$work/file_scope_basic_asm.o"

if "$minic" -S "$root/tests/compiler/c0/invalid_file_scope_basic_asm_qualifier.c" \
    -o "$work/invalid-qualifier.s" >"$work/invalid-qualifier.stdout" 2>"$work/invalid-qualifier.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/file-scope-basic-asm: qualifier accepted at file scope' >&2
    exit 1
fi
grep -F 'file-scope GNU basic asm does not allow qualifiers' "$work/invalid-qualifier.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_file_scope_basic_asm_operands.c" \
    -o "$work/invalid-operands.s" >"$work/invalid-operands.stdout" 2>"$work/invalid-operands.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/file-scope-basic-asm: extended operands accepted in basic-asm v0' >&2
    exit 1
fi
grep -F 'file-scope GNU basic asm does not support operands' "$work/invalid-operands.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/file-scope-basic-asm entity=translation-unit raw=verbatim aliases=asm+__asm+__asm__ strings=shared order=stable qualifiers+operands=fail-closed'
''')

# Permanent full-gate coverage.
gate = Path(".github/scripts/compiler-c0-full-gate.sh")
text = gate.read_text()
old = '''static_object_address_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-object-address" \\
        sh tests/compiler/c0/run-static-object-address-relocation.sh
}

external_cjson_frontier() {
'''
new = '''static_object_address_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-static-object-address" \\
        sh tests/compiler/c0/run-static-object-address-relocation.sh
}

file_scope_basic_asm_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-file-scope-basic-asm" \\
    HOST_CC=cc \\
        sh tests/compiler/c0/run-file-scope-basic-asm.sh
}

external_cjson_frontier() {
'''
if text.count(old) != 1:
    raise SystemExit(f"file asm full-gate helper anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
old = '''start_gate static-object-address-focused static_object_address_focused
start_gate wide-string-focused wide_string_focused
'''
new = '''start_gate static-object-address-focused static_object_address_focused
start_gate file-scope-basic-asm-focused file_scope_basic_asm_focused
start_gate wide-string-focused wide_string_focused
'''
if text.count(old) != 1:
    raise SystemExit(f"file asm full-gate start anchor mismatch: {text.count(old)}")
gate.write_text(text.replace(old, new, 1))

#include "linker_script.h"

#include <stdio.h>
#include <string.h>

static bool resolve_symbol(void *context,
                           const char *name,
                           uint64_t *value_out) {
    (void)context;
    if (strcmp(name, "base") == 0) {
        *value_out = UINT64_C(0x1000);
        return true;
    }
    if (strcmp(name, "kernel_entry") == 0) {
        *value_out = UINT64_C(0xffffffff80001234);
        return true;
    }
    if (strcmp(name, "_start") == 0) {
        *value_out = UINT64_C(0x200000);
        return true;
    }
    return false;
}

static bool resolve_section(void *context,
                            const char *name,
                            uint64_t *value_out) {
    (void)context;
    if (strcmp(name, ".text") == 0) {
        *value_out = UINT64_C(0x2000);
        return true;
    }
    return false;
}

static bool resolve_section_size(void *context,
                                 const char *name,
                                 uint64_t *value_out) {
    if (strcmp(name, ".got.plt") == 0) {
        *value_out = *(const uint64_t *)context;
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    MiniLdScript script;
    MiniLdScriptEvalContext context;
    MiniLdScriptPattern pattern;
    uint64_t value;

    if (argc != 2) {
        return 2;
    }
    minild_script_initialize(&script);
    if (!minild_script_parse_file(argv[1], &script, stderr)) {
        return 1;
    }
    if (script.command_count != 12U ||
        script.expression_count != 37U ||
        script.entry_symbol == NULL ||
        strcmp(script.entry_symbol, "_start") != 0 ||
        script.output_arch == NULL ||
        strcmp(script.output_arch, "riscv") != 0) {
        minild_script_destroy(&script);
        return 1;
    }

    /* OUTPUT_ARCH admits GNU's exact target names without relaxing the
     * lexer for symbol expressions or the separate ENTRY directive. */
    {
        MiniLdScript x86;
        MiniLdScript malformed;

        minild_script_initialize(&x86);
        if (!minild_script_parse_file("tests/linker/output-arch-x86.lds",
                                      &x86, stderr) ||
            x86.output_arch == NULL ||
            strcmp(x86.output_arch, "i386:x86-64") != 0 ||
            x86.entry_symbol == NULL ||
            strcmp(x86.entry_symbol, "_start") != 0) {
            minild_script_destroy(&x86);
            minild_script_destroy(&script);
            return 1;
        }
        minild_script_destroy(&x86);
        minild_script_initialize(&malformed);
        if (minild_script_parse_file("tests/linker/output-arch-invalid.lds",
                                      &malformed, stderr)) {
            minild_script_destroy(&malformed);
            minild_script_destroy(&script);
            return 1;
        }
        minild_script_destroy(&malformed);
        minild_script_initialize(&malformed);
        if (minild_script_parse_file("tests/linker/output-entry-invalid.lds",
                                      &malformed, stderr)) {
            minild_script_destroy(&malformed);
            minild_script_destroy(&script);
            return 1;
        }
        minild_script_destroy(&malformed);
        puts("MINILD_SCRIPT_ARCH=PASS GNU_i386_colon_dash=1 invalid_arch=reject invalid_ENTRY=reject");
    }

    memset(&pattern, 0, sizeof(pattern));
    pattern.text = ".text.*";
    if (!minild_script_pattern_matches(&pattern, ".text.hot") ||
        minild_script_pattern_matches(&pattern, ".data")) {
        minild_script_destroy(&script);
        return 1;
    }

    memset(&context, 0, sizeof(context));
    context.dot = UINT64_C(0x2103);
    context.resolve_symbol = resolve_symbol;
    context.resolve_section = resolve_section;
    if (!minild_script_evaluate(&script,
                                script.commands[1].value.expression,
                                &context,
                                &value,
                                stderr) ||
        value != UINT64_C(0xffffffff80000000)) {
        minild_script_destroy(&script);
        return 1;
    }

    /* Real vDSO layout uses SIZEOF_HEADERS after PHDRS defines the exact
     * ELF64 program-header count. This must never silently evaluate to zero. */
    {
        MiniLdScript vdso;
        MiniLdScriptEvalContext vdso_context;
        MiniLdScriptExprId expr_id;
        bool found = false;
        size_t i;
        minild_script_initialize(&vdso);
        if (!minild_script_parse_file(
                "tests/linker/vdso-expression.lds", &vdso, stderr)) {
            minild_script_destroy(&script);
            return 1;
        }
        for (i = 0U; i < vdso.command_count; ++i) {
            if (vdso.commands[i].kind == MINILD_SCRIPT_SET_DOT) {
                expr_id = vdso.commands[i].value.expression;
                found = true;
                break;
            }
        }
        memset(&vdso_context, 0, sizeof(vdso_context));
        if (!found ||
            minild_script_evaluate(&vdso, expr_id, &vdso_context,
                                   &value, stderr)) {
            minild_script_destroy(&vdso);
            minild_script_destroy(&script);
            return 1;
        }
        if (vdso.phdr_count != 4U ||
            strcmp(vdso.phdrs[0].name, "text") != 0 ||
            strcmp(vdso.phdrs[0].type_name, "PT_LOAD") != 0 ||
            !vdso.phdrs[0].filehdr || !vdso.phdrs[0].phdrs ||
            !vdso.phdrs[0].have_flags || vdso.phdrs[0].flags != 5U ||
            strcmp(vdso.phdrs[3].type_name, "PT_GNU_EH_FRAME") != 0 ||
            vdso.version_name == NULL ||
            strcmp(vdso.version_name, "LINUX_4.15") != 0 ||
            vdso.version_global_count != 7U ||
            !vdso.version_local_all ||
            strcmp(vdso.version_globals[6], "__vdso_riscv_hwprobe") != 0 ||
            vdso.commands[3].kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            vdso.commands[3].value.section.phdr_name_count != 1U ||
            strcmp(vdso.commands[3].value.section.phdr_names[0],
                   "text") != 0) {
            minild_script_destroy(&vdso);
            minild_script_destroy(&script);
            return 1;
        }
        vdso_context.have_headers_size = true;
        vdso_context.headers_size = UINT64_C(64) + 4U * UINT64_C(56);
        if (!minild_script_evaluate(&vdso, expr_id, &vdso_context,
                                    &value, stderr) ||
            value != UINT64_C(288)) {
            minild_script_destroy(&vdso);
            minild_script_destroy(&script);
            return 1;
        }
        minild_script_destroy(&vdso);
        puts("MINILD_SCRIPT_VDSO_HEADERS=PASS PHDR_count=4 sizeof_headers=288 mapping=text flags=5 filehdr=true VERSION=LINUX_4.15 exports=7 local_all=true no_guess=true");
    }
    {
        static const struct {
            const char *name;
            uint64_t expected;
            bool valid;
        } bitwise_cases[] = {
            {"_kernel_entry", UINT64_C(0xffff80001234), true},
            {"_precedence", UINT64_C(1), true},
            {"_xor", UINT64_C(3), true},
            {"_shift", UINT64_C(2), true},
            {"_modulo", UINT64_C(2), true},
            {"_complement", UINT64_C(0), true},
            {"_invalid_shift", UINT64_C(0), false},
            {"_invalid_modulo", UINT64_C(0), false}
        };
        MiniLdScript bitwise;
        size_t c;

        minild_script_initialize(&bitwise);
        if (!minild_script_parse_file(
                "tests/linker/script-gnu-bitwise-expressions.lds",
                &bitwise, stderr) ||
            bitwise.output_arch == NULL ||
            strcmp(bitwise.output_arch, "loongarch") != 0) {
            minild_script_destroy(&bitwise);
            minild_script_destroy(&script);
            return 1;
        }
        for (c = 0U; c < sizeof(bitwise_cases) / sizeof(bitwise_cases[0]); ++c) {
            bool found = false;
            size_t i;

            for (i = 0U; i < bitwise.command_count; ++i) {
                const MiniLdScriptCommand *command = &bitwise.commands[i];
                bool evaluated;

                if (command->kind != MINILD_SCRIPT_DEFINE_SYMBOL ||
                    strcmp(command->value.symbol.name,
                           bitwise_cases[c].name) != 0) {
                    continue;
                }
                found = true;
                evaluated = minild_script_evaluate(
                    &bitwise, command->value.symbol.expression,
                    &context, &value, stderr);
                if (evaluated != bitwise_cases[c].valid ||
                    (evaluated && value != bitwise_cases[c].expected)) {
                    minild_script_destroy(&bitwise);
                    minild_script_destroy(&script);
                    return 1;
                }
                break;
            }
            if (!found) {
                minild_script_destroy(&bitwise);
                minild_script_destroy(&script);
                return 1;
            }
        }
        minild_script_destroy(&bitwise);
        puts("MINILD_SCRIPT_GNU_BITWISE=PASS loongarch_kernel_mask=exact precedence=AND_XOR_OR_shift modulo=PASS invalid_shift_divisor=reject");
    }
    {
        MiniLdScript kbuild;
        MiniLdScriptEvalContext check;
        uint64_t got_size = 0U;
        minild_script_initialize(&kbuild);
        if (!minild_script_parse_file(
                "tests/linker/script-x86-kbuild-language.lds", &kbuild,
                stderr) ||
            kbuild.command_count != 8U ||
            kbuild.output_format == NULL ||
            strcmp(kbuild.output_format, "elf64-x86-64") != 0 ||
            kbuild.phdr_count != 2U ||
            kbuild.commands[4].kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            !kbuild.commands[4].value.section.info ||
            kbuild.commands[5].kind != MINILD_SCRIPT_ASSERT ||
            kbuild.commands[6].kind != MINILD_SCRIPT_DEFINE_SYMBOL ||
            !kbuild.commands[6].value.symbol.provide ||
            kbuild.commands[7].kind != MINILD_SCRIPT_SET_DOT) {
            minild_script_destroy(&kbuild);
            minild_script_destroy(&script);
            return 1;
        }
        memset(&check, 0, sizeof(check));
        check.user = &got_size;
        check.resolve_symbol = resolve_symbol;
        check.resolve_section_size = resolve_section_size;
        if (!minild_script_evaluate(&kbuild,
                                    kbuild.commands[0].value.expression,
                                    &check, &value, stderr) ||
            value != UINT64_C(0x200000) ||
            !minild_script_evaluate(&kbuild,
                                    kbuild.commands[5].value.expression,
                                    &check, &value, stderr) ||
            value != 1U ||
            !minild_script_evaluate(&kbuild,
                                    kbuild.commands[7].value.expression,
                                    &check, &value, stderr) ||
            value != 1U) {
            minild_script_destroy(&kbuild);
            minild_script_destroy(&script);
            return 1;
        }
        got_size = 7U;
        if (minild_script_evaluate(&kbuild,
                                   kbuild.commands[5].value.expression,
                                   &check, &value, stderr)) {
            minild_script_destroy(&kbuild);
            minild_script_destroy(&script);
            return 1;
        }
        minild_script_destroy(&kbuild);
        puts("MINILD_SCRIPT_X86_KBUILD=PASS output_format phdrs INFO at ALIGN2 SIZEOF ASSERT provide plus_equals checks");
    }
    puts("MINILD_SCRIPT_A0=PASS linux-subset=PASS commands=12 expressions=37 wildcard=PASS");
    minild_script_destroy(&script);
    return 0;
}

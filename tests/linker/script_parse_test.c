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
        puts("MINILD_SCRIPT_VDSO_HEADERS=PASS PHDR_count=4 sizeof_headers=288 mapping=text flags=5 filehdr=true no_guess=true");
    }
    puts("MINILD_SCRIPT_A0=PASS linux-subset=PASS commands=12 expressions=37 wildcard=PASS");
    minild_script_destroy(&script);
    return 0;
}

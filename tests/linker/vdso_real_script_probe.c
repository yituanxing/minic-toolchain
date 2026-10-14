/* Independent read-only parse oracle for the actual preprocessed Linux
 * 6.6.143 arch/riscv/kernel/vdso/vdso.lds. Parsing is NOT permission
 * to accept -T until section placement, PHDRS and versions are emitted. */
#include "linker_script.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    MiniLdScript script;
    MiniLdScriptEvalContext eval;
    size_t i, provides = 0U, mapped = 0U, sizeof_exprs = 0U;
    uint64_t value = 0U;
    int result = 1;

    if (argc != 2) {
        return 2;
    }
    minild_script_initialize(&script);
    if (!minild_script_parse_file(argv[1], &script, stderr)) {
        goto done;
    }
    if (script.phdr_count != 4U || script.version_name == NULL ||
        strcmp(script.version_name, "LINUX_4.15") != 0 ||
        script.version_global_count != 7U || !script.version_local_all ||
        strcmp(script.phdrs[0].name, "text") != 0 ||
        strcmp(script.phdrs[0].type_name, "PT_LOAD") != 0 ||
        !script.phdrs[0].have_flags || script.phdrs[0].flags != 5U ||
        !script.phdrs[0].filehdr || !script.phdrs[0].phdrs ||
        strcmp(script.phdrs[1].type_name, "PT_DYNAMIC") != 0 ||
        strcmp(script.phdrs[2].type_name, "PT_NOTE") != 0 ||
        strcmp(script.phdrs[3].type_name, "PT_GNU_EH_FRAME") != 0) {
        fprintf(stderr, "MINILD_VDSO_SCRIPT=FAIL missing_program_header_or_version_contract\n");
        goto done;
    }
    for (i = 0U; i < script.command_count; ++i) {
        const MiniLdScriptCommand *c = &script.commands[i];
        if (c->kind == MINILD_SCRIPT_DEFINE_SYMBOL &&
            c->value.symbol.provide) {
            ++provides;
        }
        if (c->kind == MINILD_SCRIPT_OUTPUT_SECTION &&
            c->value.section.phdr_name_count != 0U) {
            ++mapped;
        }
    }
    memset(&eval, 0, sizeof(eval));
    eval.have_headers_size = true;
    eval.headers_size = 64U + script.phdr_count * 56U;
    for (i = 0U; i < script.expression_count; ++i) {
        if (script.expressions[i].kind !=
            MINILD_SCRIPT_EXPR_SIZEOF_HEADERS) {
            continue;
        }
        if (!minild_script_evaluate(&script, i, &eval, &value, stderr) ||
            value != UINT64_C(288)) {
            fprintf(stderr, "MINILD_VDSO_SCRIPT=FAIL sizeof_headers\n");
            goto done;
        }
        ++sizeof_exprs;
    }
    if (provides != 2U || sizeof_exprs != 1U || mapped < 5U) {
        fprintf(stderr,
                "MINILD_VDSO_SCRIPT=FAIL provides=%zu sizeof=%zu mapped=%zu\n",
                provides, sizeof_exprs, mapped);
        goto done;
    }
    printf("MINILD_VDSO_REAL_SCRIPT_PARSE=PASS phdrs=4 mapped_sections=%zu provides=2 sizeof_headers=288 version=LINUX_4.15 exports=7 local_all=true emission_claim=false\n",
           mapped);
    result = 0;
done:
    minild_script_destroy(&script);
    return result;
}

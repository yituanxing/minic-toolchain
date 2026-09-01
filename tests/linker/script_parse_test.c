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

    puts("MINILD_SCRIPT_A0=PASS linux-subset=PASS commands=12 expressions=37 wildcard=PASS");
    minild_script_destroy(&script);
    return 0;
}

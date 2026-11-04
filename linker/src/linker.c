/* MiniLD static ET_EXEC driver. Input projection, script selection and
 * reachability are included as separate phase owners below; archive and
 * source admission belong to the shared Core. */
#include <limits.h>

#include "linker_core.inc"

static bool minild_gc_sections_enabled;

void minild_cli_enable_gc_sections_v0(void);
int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics);
int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

void minild_cli_enable_gc_sections_v0(void) {
    minild_gc_sections_enabled = true;
}

#include "linker_input_projection.inc"
#include "linker_script_input_selection.inc"
#include "linker_input_gc.inc"

static int minild_link_static_pipeline(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    bool garbage_collect,
    FILE *diagnostics) {
    MiniLdState state;
    MiniLdLayout layout;
    MiniLdOutputPlan output_plan = {0};
    MiniLdStaticGot got;
    MiniLdScript script;
    const char *entry_symbol;
    uint64_t entry = 0U;
    bool have_script = false;
    bool ok = false;
    clock_t phase_start = clock();

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&got, 0, sizeof(got));
    minild_script_initialize(&script);
    got.section = SIZE_MAX;
    state.diagnostics = diagnostics;
    if (options != NULL && options->script_path != NULL) {
        if (!minild_script_parse_file(options->script_path, &script,
                                      diagnostics)) {
            goto done;
        }
        if (script.output_arch != NULL &&
            strcmp(script.output_arch, "riscv") != 0) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-linker-script-arch:%s\n",
                    script.output_arch);
            goto done;
        }
        if (script.phdr_count != 0U || script.version_name != NULL) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-static-script-PHDRS-or-VERSION\n");
            goto done;
        }
        have_script = true;
    }
    entry_symbol = options != NULL && options->entry_symbol != NULL
                       ? options->entry_symbol
                       : have_script && script.entry_symbol != NULL
                             ? script.entry_symbol
                             : "_start";

    if (!process_input_sequence(&state, inputs, input_count)) {
        goto done;
    }
    minild_perf_phase("inputs", &phase_start, &state, diagnostics);

    if (!state.have_input ||
        !minild_project_admitted_inputs(
            &state,
            have_script ? MINILD_PROJECTION_SCRIPT :
            garbage_collect ? MINILD_PROJECTION_GC :
                              MINILD_PROJECTION_MERGED) ||
        ((garbage_collect || have_script) &&
         !minild_restore_input_order(&state)) ||
        !minild_allocate_common(&state) ||
        (garbage_collect && have_script &&
         !minild_select_script_inputs(&state, &script)) ||
        (garbage_collect && !minild_gc_prune_static(&state, entry_symbol)) ||
        (garbage_collect && !have_script &&
         (!minild_gc_group_default_output_classes(&state) ||
          !minild_gc_sort_default_output_alignment(&state))) ||
        !static_build_got(&state, &got) ||
        !(have_script
              ? static_build_script_layout(&state, &script, &layout, 0U)
              : static_build_layout(&state, &layout))) {
        goto done;
    }
    minild_perf_phase("layout", &phase_start, &state, diagnostics);

    if (!static_synthesize_runtime_boundaries(&state, &layout) ||
        !static_fill_got(&state, &layout, &got) ||
        !static_apply_relocations(&state, &layout, &got)) {
        goto done;
    }
    minild_perf_phase("relocations", &phase_start, &state, diagnostics);

    if (!static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !minild_output_plan_build(&state, &layout, &output_plan) ||
        !static_write_executable(&state, &layout, &output_plan,
                                 output_path, entry)) {
        goto done;
    }
    minild_perf_phase("output", &phase_start, &state, diagnostics);
    ok = true;

done:
    minild_output_plan_destroy(&output_plan);
    minild_layout_destroy(&layout);
    minild_script_destroy(&script);
    static_got_destroy(&got);
    state_destroy(&state);
    minild_perf_phase("cleanup", &phase_start, &state, diagnostics);
    return ok ? 0 : 1;
}

int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics) {
    return minild_link_static_pipeline(output_path,
                                       inputs,
                                       input_count,
                                       options,
                                       minild_gc_sections_enabled,
                                       diagnostics);
}

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdStaticOptions options;

    options.entry_symbol = entry_symbol;
    options.script_path = NULL;
    return minild_link_static_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}

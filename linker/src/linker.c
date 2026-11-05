/* MiniLD static ET_EXEC driver. Input projection, script selection and
 * reachability are included as separate phase owners below; archive and
 * source admission belong to the shared Core. */
#include <limits.h>

#include "minild_elf32_vdso.h"
#include "linker_core.inc"

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

#include "linker_input_projection.inc"
#include "linker_script_input_selection.inc"
#include "linker_input_gc.inc"
#include "linker_merge_sections.inc"
#include "linker_static_build_id.inc"

static int minild_link_static_pipeline(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    bool garbage_collect,
    const MiniLdTargetProfile *target,
    FILE *diagnostics) {
    MiniLdState state;
    MiniLdLayout layout;
    MiniLdOutputPlan output_plan = {0};
    MiniLdStaticTargetPlan static_reloc_plan = {0};
    MiniLdScript script;
    const char *entry_symbol;
    uint64_t entry = 0U;
    bool have_script = false;
    bool ok = false;
    clock_t phase_start = clock();

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL || target == NULL || !target->supports_static ||
        (target->elf_class != ELFCLASS64 &&
         target->elf_class != ELFCLASS32)) {
        return 2;
    }
    if (options != NULL &&
        ((options->script_path != NULL && !target->supports_static_script) ||
         (options->gc_sections && !target->supports_static_gc))) {
        fprintf(diagnostics,
                "minic-ld: unsupported-target-static-script-or-gc:%s\n",
                target->emulation);
        return 2;
    }
    if (options != NULL && options->nmagic && options->script_path == NULL) {
        fprintf(diagnostics,
                "minic-ld: --nmagic-requires-static-script\n");
        return 2;
    }
    if (options != NULL && options->fix_cortex_a53_843419 &&
        target->machine != EM_AARCH64) {
        fprintf(diagnostics,
                "minic-ld: --fix-cortex-a53-843419-requires-aarch64\n");
        return 2;
    }
    if (options != NULL && options->max_page_size != 0U &&
        (options->max_page_size > SIZE_MAX ||
         options->max_page_size < target->page_size ||
         (options->max_page_size & (options->max_page_size - 1U)) != 0U)) {
        fprintf(diagnostics,
                "minic-ld: invalid-max-page-size:%llu\n",
                (unsigned long long)options->max_page_size);
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.target = target;
    state.nmagic = options != NULL && options->nmagic;
    state.static_pie_requested =
        options != NULL && options->static_pie_requested;
    state.kernel_shared_image =
        options != NULL && options->kernel_shared_no_dynamic_relocations;
    state.fix_cortex_a53_843419 =
        options != NULL && options->fix_cortex_a53_843419;
    state.max_page_size =
        options != NULL ? options->max_page_size : 0U;
    state.separate_code =
        options != NULL && options->separate_code_explicit
            ? options->separate_code : target->separate_code_default;
    state.orphan_handling_error =
        options != NULL && options->orphan_handling_error;
    state.build_id_sha1 =
        options != NULL && options->build_id_sha1;
    state.discard_locals = options != NULL && options->discard_locals;
    memset(&layout, 0, sizeof(layout));
    minild_script_initialize(&script);
    state.diagnostics = diagnostics;
    if (options != NULL && options->script_path != NULL) {
        if (!minild_script_parse_file(options->script_path, &script,
                                      diagnostics)) {
            goto done;
        }
        if (script.output_arch != NULL &&
            !minild_target_accept_script_arch(state.target,
                                              script.output_arch)) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-linker-script-arch:%s\n",
                    script.output_arch);
            goto done;
        }
        if (script.output_format != NULL &&
            strcmp(script.output_format,
                   state.target->script_output_format) != 0) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-linker-script-format:%s"
                    ":target=%s\n",
                    script.output_format,
                    state.target->script_output_format);
            goto done;
        }
        if (script.version_name != NULL) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-static-script-VERSION\n");
            goto done;
        }
        have_script = true;
    }
    entry_symbol = options != NULL && options->entry_symbol != NULL
                       ? options->entry_symbol
                       : have_script && script.entry_symbol != NULL
                             ? script.entry_symbol
                             : "_start";

    /* Forced undefined roots must exist before archive admission: this
     * allows the ordinary resolver/extractor to pull unreferenced members
     * without fabricating an input object or changing archive ordering. */
    if (options != NULL) {
        size_t forced_index;

        for (forced_index = 0U;
             forced_index < options->forced_undefined_count;
             ++forced_index) {
            size_t resolved_index;
            const char *name = options->forced_undefined_symbols[forced_index];

            if (name == NULL || *name == '\0' ||
                !symbol_resolver_merge_global(
                    &state.resolver, diagnostics, name, MINILD_SECTION_UNDEF,
                    0U, 0U, ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                    STV_DEFAULT, SIZE_MAX, &resolved_index)) {
                fprintf(diagnostics,
                        "minic-ld: invalid-forced-undefined-symbol\n");
                goto done;
            }
        }
    }
    if (have_script) {
        size_t extern_index;

        for (extern_index = 0U;
             extern_index < script.extern_symbol_count; ++extern_index) {
            size_t resolved_index;
            const char *name = script.extern_symbols[extern_index];

            /* GNU EXTERN has -u semantics.  It must enter the canonical
             * resolver before archive extraction, not in layout. */
            if (!symbol_resolver_merge_global(
                    &state.resolver, diagnostics, name,
                    MINILD_SECTION_UNDEF, 0U, 0U,
                    ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), STV_DEFAULT,
                    SIZE_MAX, &resolved_index)) {
                fprintf(diagnostics,
                        "minic-ld: invalid-script-EXTERN-symbol:%s\n", name);
                goto done;
            }
        }
    }
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
        /* Source ownership (in particular /DISCARD/) must be known before
         * targets reserve synthetic relocation storage, even without GC.
         * Layout still owns the final output placement decision. */
        (have_script && !minild_select_script_inputs(&state, &script)) ||
        (state.kernel_shared_image &&
         !minild_target_validate_kernel_shared_relocations(&state)) ||
        (garbage_collect &&
         !minild_gc_prune_static(&state, entry_symbol, options)) ||
        (garbage_collect && !have_script &&
         (!minild_gc_group_default_output_classes(&state) ||
          !minild_gc_sort_default_output_alignment(&state))) ||
        !minild_merge_live_sections(&state, have_script ? &script : NULL) ||
        !minild_static_reserve_build_id(&state) ||
        !static_reserve_target_relocations(&state, &static_reloc_plan) ||
        !static_run_layout_to_fixed_point(&state,
                                          have_script ? &script : NULL,
                                          &layout, &static_reloc_plan)) {
        goto done;
    }
    minild_perf_phase("layout", &phase_start, &state, diagnostics);

    if (!static_synthesize_runtime_boundaries(&state, &layout) ||
        !static_fill_target_relocations(&state, &layout, &static_reloc_plan) ||
        !static_apply_relocations(&state, &layout, &static_reloc_plan)) {
        goto done;
    }
    minild_perf_phase("relocations", &phase_start, &state, diagnostics);

    if (!static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !minild_output_plan_build(&state, &layout, &output_plan) ||
        !static_write_executable(&state, &layout, &output_plan,
                                 output_path, entry,
                                 options != NULL && options->output_et_dyn,
                                 options != NULL && options->emit_relocs,
                                 have_script ? &script : NULL)) {
        goto done;
    }
    minild_perf_phase("output", &phase_start, &state, diagnostics);
    ok = true;

done:
    free(state.script_forward_guesses);
    minild_output_plan_destroy(&output_plan);
    minild_layout_destroy(&layout);
    minild_script_destroy(&script);
    static_target_plan_destroy(&state, &static_reloc_plan);
    state_destroy(&state);
    minild_perf_phase("cleanup", &phase_start, &state, diagnostics);
    return ok ? 0 : 1;
}

int minild_link_static_elf64_inputs_target(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    const char *emulation,
    FILE *diagnostics) {
    return minild_link_static_pipeline(output_path,
                                       inputs,
                                       input_count,
                                       options,
                                       options != NULL && options->gc_sections,
                                       minild_target_find(emulation),
                                       diagnostics);
}

int minild_link_shared_inputs_target(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    const char *emulation,
    FILE *diagnostics) {
    const MiniLdTargetProfile *target = minild_target_find(emulation);
    const MiniLdLinkMode mode = options != NULL && options->pie
                                    ? MINILD_LINK_PIE : MINILD_LINK_SHARED;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL || target == NULL ||
        !minild_emulation_supports_mode(emulation, mode)) {
        if (diagnostics != NULL) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-target-shared-mode:%s\n",
                    emulation == NULL ? "(null)" : emulation);
        }
        return 2;
    }
    if (target->link_shared != NULL) {
        return target->link_shared(output_path, inputs, input_count,
                                   options, diagnostics);
    }
    fprintf(diagnostics,
            "minic-ld: unimplemented-target-shared-backend:%s\n",
            emulation);
    return 2;
}

int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics) {
    return minild_link_static_elf64_inputs_target(
        output_path, inputs, input_count, options, "elf64lriscv",
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
    options.forced_undefined_symbols = NULL;
    options.forced_undefined_count = 0U;
    options.gc_sections = false;
    options.nmagic = false;
    options.discard_locals = false;
    options.output_et_dyn = false;
    options.static_pie_requested = false;
    options.fix_cortex_a53_843419 = false;
    options.max_page_size = 0U;
    options.orphan_handling_error = false;
    options.build_id_sha1 = false;
    options.kernel_shared_no_dynamic_relocations = false;
    options.emit_relocs = false;
    return minild_link_static_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}

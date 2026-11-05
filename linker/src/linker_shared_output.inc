/* ET_DYN output driver: validation, final writer and shared-link entry.
 * Pre-layout, post-layout and PHDR policies have distinct owners. */
#include "linker_shared_target_abi.inc"
#include "linker_shared_pre_layout.inc"
#include "linker_shared_finalize.inc"
#include "linker_shared_program_plan.inc"

static bool shared_write_object(MiniLdState *state,
                                const MiniLdLayout *layout,
                                MiniLdOutputPlan *output_plan,
                                const MiniLdSharedImage *shared,
                                const char *path,
                                uint64_t entry) {
    MiniLdFinalImage final = {0};
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    size_t image_size;
    bool ok = false;

    /* This ET_DYN writer still emits ELF64 dynamic records.  Keep that
     * admission explicit while sharing the generic ELF header-size model. */
    if (state->target->elf_class != ELFCLASS64 ||
        abi.ehdr != sizeof(Elf64_Ehdr) ||
        abi.phdr != sizeof(Elf64_Phdr)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-elf-class\n");
        goto done;
    }
    if (!minild_shared_program_plan_build(state, layout, output_plan, shared,
                                          &final.program)) {
        goto done;
    }
    if (final.program.count > (SIZE_MAX - abi.ehdr) / abi.phdr) {
        fprintf(state->diagnostics,
                "minic-ld: shared-program-header-overflow\n");
        goto done;
    }
    image_size = abi.ehdr + final.program.count * abi.phdr;

    if (output_plan->count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-shared-sections\n");
        goto done;
    }
    if (layout->have_rx &&
        layout->rx_file_offset + layout->rx_file_size > image_size) {
        image_size = layout->rx_file_offset + layout->rx_file_size;
    }
    if (layout->have_rw &&
        layout->rw_file_offset + layout->rw_file_size > image_size) {
        image_size = layout->rw_file_offset + layout->rw_file_size;
    }
    /* GNU nm reads the final .symtab from the vDSO, not only .dynsym.
     * Synthetic non-ALLOC names and symbols are after the LOAD image.
     * Dynamic section-header links remain ET_DYN-specific policy. */
    if (!minild_final_image_prepare(state, layout, output_plan,
                                          &final, image_size,
                                          shared->version_script,
                                          MINILD_FINAL_STRTAB_FIRST,
                                          ET_DYN, false)) {
        goto done;
    }
    minild_shared_output_section_links(output_plan, shared,
                                        final.tables.headers);
    ok = minild_final_write_image(state, layout, output_plan, &final.program,
                                  &final.tables, &final.symbols,
                                  &final.relocations,
                                  ET_DYN, entry, path);

done:
    /* The old writer removed partial output even when planning failed before
     * image allocation.  Preserve that fail-closed contract; the common ELF
     * serializer also removes any file it may have opened. */
    if (!ok) {
        (void)remove(path);
    }
    minild_final_image_destroy(&final);
    return ok;
}

static bool shared_validate_pie_dso_symbols(
    const MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        unsigned bind = ELF64_ST_BIND(symbol->info);

        if (symbol->section != MINILD_SECTION_UNDEF ||
            symbol->script_planned_defined ||
            bind == STB_WEAK ||
            bind == STB_LOCAL ||
            symbol->name[0] == '\0') {
            continue;
        }
        if (!state_dso_defines(state, symbol->name)) {
            fprintf(state->diagnostics,
                    "minic-ld: unresolved-pie-symbol:%s\n",
                    symbol->name);
            return false;
        }
    }
    return true;
}

/* -S on an already debug-free ELF shared input is a semantic no-op.
 * Reject unimplemented debug removal rather than silently emit a DSO
 * still containing non-ALLOC debugging sections. .eh_frame is runtime
 * unwind data and is intentionally not classified as debug information.
 */
static bool shared_strip_debug_preflight(const MiniLdState *state) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        const char *name = section->name;

        if (section->size == 0U) {
            continue;
        }
        if (minild_is_debug_section_name(name)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-strip-debug-section:%s\n",
                    name);
            return false;
        }
    }
    return true;
}

/* Shared metadata/dynamic relocation slots are prepared before the vDSO
 * script places its inputs.  Until these builders consume the same liveness
 * map as the final OutputPlan, silently discarding an allocated source could
 * leave a dynamic relocation or exported symbol pointing into dead bytes.
 * Reject that exact unsupported case instead of emitting an invalid DSO. */
static bool shared_script_validate_discarded_alloc(
    const MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];

        if (layout->work[i].discarded &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-discard-alloc:%s\n",
                    script_section_source_name(section));
            return false;
        }
    }
    return true;
}

typedef struct MiniLdSharedRiscvRelaxPolicy {
    const MiniLdState *state;
    const MiniLdSharedImage *shared;
} MiniLdSharedRiscvRelaxPolicy;

/* Preemptible references must still branch through their reserved PLT
 * entry.  The shared finalizer owns CALL/PLT patching and currently admits
 * full AUIPC/JALR pairs only; a short JAL in this class would otherwise be
 * incorrectly patched against S instead of PLT(S).  Locally bound calls are
 * safe to shorten and are patched by the existing final JAL/RVC writer. */
static bool shared_riscv_relax_local_call(const MiniLdReloc *reloc,
                                          void *opaque) {
    const MiniLdSharedRiscvRelaxPolicy *policy = opaque;
    if (reloc->symbol == SIZE_MAX ||
        reloc->symbol >= policy->shared->prepared_symbol_count) {
        return false;
    }
    return !shared_symbol_needs_plt(policy->state, policy->shared,
                                     reloc->symbol);
}

/* Both default ET_DYN placement and the constrained vDSO -T script now use
 * the same RISC-V materializer as ET_EXEC.  Metadata sizes and symbol/PLT
 * identities were reserved before entering this loop; only machine bytes,
 * section lengths and source-coordinate offsets may change. */
static bool shared_riscv_layout_to_fixed_point(
    MiniLdState *state, const MiniLdScript *script, MiniLdLayout *layout,
    const MiniLdSharedImage *shared, MiniLdRiscvRelaxPlan **relax) {
    MiniLdScriptLayoutCheckpoint checkpoint;
    MiniLdSharedRiscvRelaxPolicy policy = {state, shared};
    const unsigned max_rounds = 32U;
    unsigned round;
    bool ok = false;

    if (!static_script_checkpoint_begin(state, script, &checkpoint)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:shared-layout-checkpoint\n");
        free(checkpoint.script_symbols);
        return false;
    }
    for (round = 0U; round < max_rounds; ++round) {
        bool changed = false;
        if (!(script != NULL
                  ? shared_build_vdso_script_layout(
                        state, script, layout)
                  : shared_build_layout(state, layout))) {
            goto done;
        }
        if (!minild_riscv_replan_image_layout(
                state, layout, relax, &changed,
                shared_riscv_relax_local_call, &policy)) {
            goto done;
        }
        if (!changed) {
            ok = true;
            goto done;
        }
        minild_layout_destroy(layout);
        if (!static_script_checkpoint_rollback(state, &checkpoint)) {
            goto done;
        }
    }
    fprintf(state->diagnostics,
            "minic-ld: riscv-shared-layout-did-not-converge:rounds=%u\n",
            max_rounds);
done:
    free(checkpoint.script_symbols);
    return ok;
}

int minild_link_shared_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdSharedOptions *options,
    FILE *diagnostics) {
    const char *soname = options != NULL ? options->soname : NULL;
    const char *entry_symbol =
        options != NULL ? options->entry_symbol : NULL;
    const char *dynamic_list_path =
        options != NULL ? options->dynamic_list_path : NULL;
    const char *version_script_path =
        options != NULL ? options->version_script_path : NULL;
    const char *script_path =
        options != NULL ? options->script_path : NULL;
    const char *needed_name =
        options != NULL ? options->needed_name : NULL;
    const bool pie = options != NULL && options->pie;
    const bool strip_debug = options != NULL && options->strip_debug;
    const bool hash_style_both = options != NULL && options->hash_style_both;
    const bool eh_frame_hdr = options != NULL && options->eh_frame_hdr;
    const char *interpreter_path =
        options != NULL && options->interpreter_path != NULL
            ? options->interpreter_path
            : pie ? minild_target_default_pie_interpreter(
                        minild_target_find("elf64lriscv")) : NULL;
    MiniLdState state;
    MiniLdLayout layout;
    MiniLdSharedImage shared;
    MiniLdRiscvRelaxPlan *relax = NULL;
    MiniLdOutputPlan output_plan = {0};
    MiniLdScript version_script;
    MiniLdScript layout_script;
    const MiniLdScript *version = NULL;
    uint64_t entry = 0U;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    state.target = minild_target_find("elf64lriscv");
    memset(&layout, 0, sizeof(layout));
    memset(&shared, 0, sizeof(shared));
    minild_script_initialize(&version_script);
    minild_script_initialize(&layout_script);
    state.diagnostics = diagnostics;
    state.allow_dso_inputs = true;
    if (version_script_path != NULL) {
        if (!minild_script_parse_file(version_script_path,
                                      &version_script, diagnostics) ||
            version_script.version_name == NULL ||
            !version_script.version_local_all ||
            version_script.command_count != 0U ||
            version_script.phdr_count != 0U ||
            version_script.version_global_count == 0U ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-version-script\n");
            goto done;
        }
        version = &version_script;
    }
    if (script_path != NULL) {
        if (version != NULL ||
            !minild_script_parse_file(script_path,
                                      &layout_script, diagnostics) ||
            layout_script.version_name == NULL ||
            layout_script.version_global_count == 0U ||
            !layout_script.version_local_all ||
            layout_script.output_arch == NULL ||
            !minild_target_accept_script_arch(state.target,
                                              layout_script.output_arch) ||
            soname == NULL || soname[0] == '\0') {
            fprintf(diagnostics,
                    "minic-ld: unsupported-shared-script-contract:%s\n",
                    script_path);
            goto done;
        }
        version = &layout_script;
    }

    if (!process_input_sequence(&state, inputs, input_count) ||
        !state.have_input ||
        !minild_project_admitted_inputs(
            &state, script_path != NULL ? MINILD_PROJECTION_SCRIPT
                                        : MINILD_PROJECTION_MERGED) ||
        (script_path != NULL && !minild_restore_input_order(&state)) ||
        (strip_debug && !shared_strip_debug_preflight(&state)) ||
        !minild_allocate_common(&state) ||
        (script_path != NULL &&
         !minild_shared_prune_script_inputs(&state, &layout_script)) ||
        (script_path != NULL &&
         !minild_script_declare_shared_symbols(&state, &layout_script)) ||
        !shared_prepare_metadata(&state,
                                 soname,
                                 dynamic_list_path,
                                 needed_name,
                                 interpreter_path,
                                 pie,
                                 hash_style_both,
                                 eh_frame_hdr,
                                 version,
                                 &shared) ||
        (pie && !shared_validate_pie_dso_symbols(&state)) ||
        !shared_riscv_layout_to_fixed_point(
            &state, script_path != NULL ? &layout_script : NULL,
            &layout, &shared, &relax)) {
        goto done;
    }
    if (script_path != NULL &&
        !shared_script_validate_discarded_alloc(&state, &layout)) {
        goto done;
    }
    shared.layout_script = script_path != NULL ? &layout_script : NULL;
    if (state.resolver.count != shared.prepared_symbol_count) {
        fprintf(diagnostics,
                "minic-ld: script-symbol-count-changed:%zu:%zu\n",
                state.resolver.count, shared.prepared_symbol_count);
        goto done;
    }

    if (!minild_output_plan_build(&state, &layout, &output_plan) ||
        !shared_finalize_ehdr_symbol(&state, &layout) ||
        (entry_symbol != NULL &&
         !static_entry_address(&state, &layout, entry_symbol, &entry)) ||
        !shared_fill_dynsym(&state, &layout, &output_plan, &shared) ||
        !shared_fill_hash(&state, &shared) ||
        !shared_fill_gnu_hash(&state, &shared) ||
        !shared_fill_versions(&state, &shared) ||
        !shared_fill_relocations(&state, &layout, &output_plan, &shared) ||
        !shared.abi->patch_final_relocations(&state, &layout,
                                             &output_plan, &shared) ||
        !minild_eh_fill(&state, &layout, &shared) ||
        !shared.abi->finalize_plt_and_calls(&state, &layout,
                                            &output_plan, &shared) ||
        !shared_fill_dynamic(&state, &layout, &shared) ||
        !shared_write_object(&state,
                             &layout,
                             &output_plan,
                             &shared,
                             output_path,
                             entry)) {
        goto done;
    }
    ok = true;

done:
    minild_output_plan_destroy(&output_plan);
    minild_layout_destroy(&layout);
    minild_riscv_relax_plan_destroy(relax);
    shared_image_destroy(&shared);
    state_destroy(&state);
    minild_script_destroy(&version_script);
    minild_script_destroy(&layout_script);
    return ok ? 0 : 1;
}

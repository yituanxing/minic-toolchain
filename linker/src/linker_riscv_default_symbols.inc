/* GNU RISC-V ELF64 unscripted default linker script PROVIDEs data/BSS
 * markers and __global_pointer$.  These are psABI linker-output policy,
 * distinct from generic array boundaries and the symbol value view. */
static bool static_synthesize_gnu_default_boundaries(
    MiniLdState *state,
    const MiniLdLayout *layout) {
    uint64_t end = layout->have_rw
                       ? layout->rw_vaddr + (uint64_t)layout->rw_mem_size
                       : layout->rx_vaddr + (uint64_t)layout->rx_file_size;
    uint64_t data_begin = UINT64_MAX;
    uint64_t sdata_begin = UINT64_MAX;
    uint64_t bss_begin = UINT64_MAX;
    uint64_t bss_end = 0U;
    uint64_t global_pointer;
    size_t i;

    if (layout->scripted) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (layout->work != NULL &&
             layout->work[i].discarded)) {
            continue;
        }
        address = layout->work[i].vaddr;
        if ((uint64_t)section->size > UINT64_MAX - address) {
            fprintf(state->diagnostics,
                    "minic-ld: default-symbol-address-overflow\n");
            return false;
        }
        section_end = address + (uint64_t)section->size;
        if ((section->flags & SHF_WRITE) != 0U &&
            address < data_begin) {
            data_begin = address;
        }
        if ((static_section_has_prefix(section->name, ".sdata") ||
             static_section_has_prefix(section->name, ".srodata")) &&
            address < sdata_begin) {
            sdata_begin = address;
        }
        if (section->type == SHT_NOBITS &&
            (section->flags & SHF_WRITE) != 0U) {
            if (address < bss_begin) {
                bss_begin = address;
            }
            if (section_end > bss_end) {
                bss_end = section_end;
            }
        }
        if (section_end > end) {
            end = section_end;
        }
    }
    if (data_begin == UINT64_MAX) {
        data_begin = end;
    }
    if (sdata_begin == UINT64_MAX) {
        sdata_begin = data_begin;
    }
    if (bss_begin == UINT64_MAX) {
        bss_begin = end;
        bss_end = end;
    }
    if (sdata_begin > UINT64_MAX - UINT64_C(0x800) ||
        data_begin > UINT64_MAX - UINT64_C(0x800)) {
        fprintf(state->diagnostics,
                "minic-ld: default-global-pointer-overflow\n");
        return false;
    }
    /* GNU RISC-V default linker script:
     * MIN(__SDATA_BEGIN__ + 0x800,
     *     MAX(__DATA_BEGIN__ + 0x800, __BSS_END__ - 0x800)).
     * Saturate subtraction when the image is unusually small. */
    global_pointer = data_begin + UINT64_C(0x800);
    if (bss_end >= UINT64_C(0x800) &&
        bss_end - UINT64_C(0x800) > global_pointer) {
        global_pointer = bss_end - UINT64_C(0x800);
    }
    if (sdata_begin + UINT64_C(0x800) < global_pointer) {
        global_pointer = sdata_begin + UINT64_C(0x800);
    }
    return static_define_absolute_if_undefined(
               state, "__DATA_BEGIN__", data_begin) &&
           static_define_absolute_if_undefined(
               state, "__SDATA_BEGIN__", sdata_begin) &&
           static_define_absolute_if_undefined(
               state, "__BSS_END__", bss_end) &&
           static_define_absolute_if_undefined(
               state, "__bss_start", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "__global_pointer$", global_pointer) &&
           static_define_absolute_if_undefined(
               state, "_edata", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "_end", end);
}

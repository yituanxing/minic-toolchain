/* RISC-V ELF64 input/output identity and psABI-owned section policy.
 * Admission, archive selection, work projection and ELF writers consume this
 * one Target owner; none of them decide RISC-V machine or attribute rules. */

#define MINILD_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)

static bool minild_target_accept_elf(const MiniElfView *elf,
                                     uint16_t type) {
    return elf->elf_class == ELFCLASS64 &&
           elf->data_encoding == ELFDATA2LSB &&
           elf->machine == EM_RISCV &&
           elf->type == type;
}

static void minild_target_set_relocatable_identity(
    MiniElfRelocatableSpec *spec) {
    spec->elf_class = ELFCLASS64;
    spec->data_encoding = ELFDATA2LSB;
    spec->machine = EM_RISCV;
}

static void minild_target_set_final_elf_identity(Elf64_Ehdr *header) {
    header->e_ident[EI_CLASS] = ELFCLASS64;
    header->e_ident[EI_DATA] = ELFDATA2LSB;
    header->e_machine = EM_RISCV;
}

static bool minild_target_is_attributes_section(uint32_t section_type) {
    return section_type == MINILD_SHT_RISCV_ATTRIBUTES;
}

/* An attributes section has one 'A' marker followed by length-delimited
 * vendor records. Preserve even unknown vendors, checking every length. */
static bool minild_riscv_attributes_valid(const unsigned char *bytes,
                                          size_t size) {
    size_t pos = 1U;

    if (size < 7U || bytes[0] != (unsigned char)'A') {
        return false;
    }
    while (pos < size) {
        size_t vendor_end;
        uint32_t length;
        if (!range_ok(pos, 4U, size)) {
            return false;
        }
        length = (uint32_t)bytes[pos] |
                 ((uint32_t)bytes[pos + 1U] << 8U) |
                 ((uint32_t)bytes[pos + 2U] << 16U) |
                 ((uint32_t)bytes[pos + 3U] << 24U);
        if (length < 6U || !range_ok(pos, (size_t)length, size)) {
            return false;
        }
        vendor_end = pos + 4U;
        while (vendor_end < pos + (size_t)length &&
               bytes[vendor_end] != 0U) {
            ++vendor_end;
        }
        if (vendor_end == pos + (size_t)length ||
            vendor_end == pos + 4U) {
            return false;
        }
        pos += (size_t)length;
    }
    return pos == size;
}

static bool minild_target_accept_attributes(
    const MiniElfView *elf, size_t section_index, const char *name,
    const MiniElfSection *section, const unsigned char **data_out,
    size_t *size_out) {
    return strcmp(name, ".riscv.attributes") == 0 &&
           section->flags == 0U &&
           minielf_section_data(elf, section_index, data_out, size_out) &&
           minild_riscv_attributes_valid(*data_out, *size_out);
}

/* Project one leading 'A' and all following vendor subsections, exactly as
 * source admission validated. The destination is ordinary work-section data. */
static bool minild_target_append_attributes(MiniLdSection *output,
                                            const MiniLdInputSection *input) {
    return input->size != 0U && input->data != NULL &&
           section_append_data(output,
                               input->data +
                                   (output->size == 0U ? 0U : 1U),
                               input->size -
                                   (output->size == 0U ? 0U : 1U));
}

static bool minild_target_merge_elf_flags(MiniLdState *state,
                                          uint32_t incoming_flags,
                                          bool incoming_has_code,
                                          const char *path) {
    uint32_t merged_flags = 0U;
    const char *abi_reason = NULL;

    if (!minild_riscv_elf_abi_merge(
            state->elf_flags, incoming_flags,
            state->processed_object_count != 0U, incoming_has_code,
            &merged_flags, &abi_reason)) {
        fprintf(state->diagnostics,
                "minic-ld: incompatible-riscv-elf-flags:%s:%s:"
                "current=0x%08x:incoming=0x%08x\n",
                path,
                abi_reason == NULL ? "unknown" : abi_reason,
                (unsigned)state->elf_flags, (unsigned)incoming_flags);
        return false;
    }
    state->elf_flags = merged_flags;
    return true;
}

/* GNU ld --build-id=sha1 reserves a real input note before linker-script
 * matching and target synthetic relocation sizing.  It is not an opaque
 * output-writer append: KEEP(*(.note.*)) and explicit :note PHDRs must see
 * this 36-byte SHT_NOTE at ordinary layout time.  Its descriptor remains
 * zero until the complete ELF image has been serialized. */
static bool minild_static_reserve_build_id(MiniLdState *state) {
    const char *name = ".note.gnu.build-id";
    size_t index;
    MiniLdSection *section;

    if (!state->build_id_sha1) {
        return true;
    }
    if (find_section_index(state, name, hash_text(name)) != SIZE_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: ambiguous-generated-build-id-section\n");
        return false;
    }
    if (!find_or_add_section(state, name, SHT_NOTE, SHF_ALLOC,
                             4U, 0U, &index)) {
        return false;
    }
    section = &state->work.sections[index];
    if (!section_append_zero(section, 36U) || section->size != 36U) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:generated-build-id\n");
        return false;
    }
    /* Elf_External_Note: namesz=4, descsz=20, type=NT_GNU_BUILD_ID. */
    store_u32le(section->data, UINT32_C(4));
    store_u32le(section->data + 4U, UINT32_C(20));
    store_u32le(section->data + 8U, UINT32_C(3));
    memcpy(section->data + 12U, "GNU", 4U);
    return true;
}

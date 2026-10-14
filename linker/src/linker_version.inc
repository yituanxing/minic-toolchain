/* GNU ELF64 dynamic version definitions for one named export node.
 * Readelf/loader consumers use .gnu.version, .gnu.version_d and the three
 * DT_* tags. Preserve the SONAME base definition as version index 1.
 */
static bool shared_fill_versions(MiniLdState *state,
                                 const MiniLdSharedImage *shared) {
    MiniLdSection *versym, *verdef;
    const MiniLdSection *dynstr;
    Elf64_Verdef base, exported;
    Elf64_Verdaux base_aux, export_aux;
    size_t i;

    if (shared->version_script == NULL) {
        return true;
    }
    versym = &state->sections[shared->versym_section];
    verdef = &state->sections[shared->verdef_section];
    dynstr = &state->sections[shared->dynstr_section];
    if (versym->size != shared->dynsym_count * sizeof(Elf64_Half) ||
        verdef->size != 2U * (sizeof(Elf64_Verdef) + sizeof(Elf64_Verdaux)) ||
        shared->version_name_offset >= dynstr->size ||
        shared->soname_offset >= dynstr->size) {
        fprintf(state->diagnostics, "minic-ld: invalid-version-section-layout\n");
        return false;
    }
    memset(versym->data, 0, versym->size);
    memset(verdef->data, 0, verdef->size);
    for (i = 0U; i < state->symbol_count; ++i) {
        size_t dynindex = shared->dynsym_index[i];
        uint16_t version;
        if (dynindex == SIZE_MAX) {
            continue;
        }
        if (dynindex >= shared->dynsym_count) {
            return false;
        }
        version = state->symbols[i].section == MINILD_SECTION_UNDEF
                  ? UINT16_C(1) : UINT16_C(2);
        versym->data[dynindex * 2U] = (unsigned char)(version & 0xffU);
        versym->data[dynindex * 2U + 1U] = (unsigned char)(version >> 8U);
    }
    memset(&base, 0, sizeof(base));
    memset(&base_aux, 0, sizeof(base_aux));
    base.vd_version = VER_DEF_CURRENT;
    base.vd_flags = VER_FLG_BASE;
    base.vd_ndx = 1U;
    base.vd_cnt = 1U;
    base.vd_hash = shared_elf_hash(
        (const char *)(dynstr->data + shared->soname_offset));
    base.vd_aux = sizeof(base);
    base.vd_next = (Elf64_Word)(sizeof(base) + sizeof(base_aux));
    base_aux.vda_name = shared->soname_offset;
    memcpy(verdef->data, &base, sizeof(base));
    memcpy(verdef->data + sizeof(base), &base_aux, sizeof(base_aux));

    memset(&exported, 0, sizeof(exported));
    memset(&export_aux, 0, sizeof(export_aux));
    exported.vd_version = VER_DEF_CURRENT;
    exported.vd_ndx = 2U;
    exported.vd_cnt = 1U;
    exported.vd_hash = shared_elf_hash(shared->version_script->version_name);
    exported.vd_aux = sizeof(exported);
    export_aux.vda_name = shared->version_name_offset;
    memcpy(verdef->data + base.vd_next, &exported, sizeof(exported));
    memcpy(verdef->data + base.vd_next + sizeof(exported),
           &export_aux, sizeof(export_aux));
    return true;
}

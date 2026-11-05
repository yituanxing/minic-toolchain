/* GNU ELF dynamic version definitions for one named export node.
 * Readelf/loader consumers use .gnu.version, .gnu.version_d and the three
 * DT_* tags. Preserve the SONAME base definition as version index 1.
 */
static void shared_store_verdef_node(unsigned char *out, uint16_t flags,
                                    uint16_t index, uint32_t hash,
                                    uint32_t next, uint32_t name) {
    store_u16le(out, VER_DEF_CURRENT);
    store_u16le(out + 2U, flags);
    store_u16le(out + 4U, index);
    store_u16le(out + 6U, 1U); /* one Verdaux record */
    store_u32le(out + 8U, hash);
    store_u32le(out + 12U, MINILD_GNU_VERDEF_WIRE_BYTES);
    store_u32le(out + 16U, next);
    store_u32le(out + MINILD_GNU_VERDEF_WIRE_BYTES, name);
    store_u32le(out + MINILD_GNU_VERDEF_WIRE_BYTES + 4U, 0U);
}

static bool shared_fill_versions(MiniLdState *state,
                                 const MiniLdSharedImage *shared) {
    MiniLdSection *versym, *verdef;
    const MiniLdSection *dynstr;
    size_t i;

    if (shared->version_script == NULL) {
        return true;
    }
    versym = &state->work.sections[shared->versym_section];
    verdef = &state->work.sections[shared->verdef_section];
    dynstr = &state->work.sections[shared->dynstr_section];
    if (shared->dynsym_count > SIZE_MAX / MINILD_GNU_VERSYM_WIRE_BYTES ||
        versym->size != shared->dynsym_count * MINILD_GNU_VERSYM_WIRE_BYTES ||
        verdef->size != 2U * MINILD_GNU_VERDEF_NODE_WIRE_BYTES ||
        shared->version_name_offset >= dynstr->size ||
        shared->soname_offset >= dynstr->size) {
        fprintf(state->diagnostics, "minic-ld: invalid-version-section-layout\n");
        return false;
    }
    memset(versym->data, 0, versym->size);
    memset(verdef->data, 0, verdef->size);
    for (i = 0U; i < state->resolver.count; ++i) {
        size_t dynindex = shared->symbols[i].dynsym_index;
        uint16_t version;
        if (dynindex == SIZE_MAX) {
            continue;
        }
        if (dynindex >= shared->dynsym_count) {
            return false;
        }
        version = state->resolver.entries[i].section == MINILD_SECTION_UNDEF
                  ? UINT16_C(1) : UINT16_C(2);
        store_u16le(versym->data + dynindex * MINILD_GNU_VERSYM_WIRE_BYTES,
                    version);
    }
    shared_store_verdef_node(
        verdef->data, VER_FLG_BASE, 1U,
        shared_elf_hash((const char *)(dynstr->data + shared->soname_offset)),
        MINILD_GNU_VERDEF_NODE_WIRE_BYTES, shared->soname_offset);
    shared_store_verdef_node(
        verdef->data + MINILD_GNU_VERDEF_NODE_WIRE_BYTES, 0U, 2U,
        shared_elf_hash(shared->version_script->version_name),
        0U, shared->version_name_offset);
    return true;
}

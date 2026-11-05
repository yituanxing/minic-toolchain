/* i386 psABI uses SHT_REL: A lives in the original relocation-site field.
 * Only Target understands the operand's width and signed interpretation.
 * Common reads the REL wire format and projects the normalized S+A/P tuple. */
static bool minild_i386_implicit_addend(const MiniElfView *elf,
                                        size_t section_index,
                                        uint64_t offset,
                                        uint32_t relocation_type,
                                        int64_t *addend_out) {
    const unsigned char *bytes;
    size_t size;
    size_t width;
    uint32_t word;

    if (elf == NULL || addend_out == NULL ||
        elf->elf_class != ELFCLASS32 ||
        elf->machine != EM_386) {
        return false;
    }
    switch (relocation_type) {
    case R_386_NONE:
        *addend_out = 0;
        return true;
    case R_386_8:
    case R_386_PC8:
        width = 1U;
        break;
    case R_386_16:
    case R_386_PC16:
        width = 2U;
        break;
    case R_386_32:
    case R_386_PC32:
        width = 4U;
        break;
    default:
        return false;
    }
    if (!minielf_section_data(elf, section_index, &bytes, &size) ||
        offset > SIZE_MAX || !range_ok((size_t)offset, width, size)) {
        return false;
    }
    bytes += (size_t)offset;
    if (width == 1U) {
        *addend_out = (int64_t)(int8_t)bytes[0];
    } else if (width == 2U) {
        *addend_out = (int64_t)(int16_t)((uint16_t)bytes[0] |
                                         (uint16_t)((uint16_t)bytes[1] << 8U));
    } else {
        word = (uint32_t)bytes[0] |
               ((uint32_t)bytes[1] << 8U) |
               ((uint32_t)bytes[2] << 16U) |
               ((uint32_t)bytes[3] << 24U);
        *addend_out = (int64_t)(int32_t)word;
    }
    return true;
}

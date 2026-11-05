/* GNU BFD's ELF checksum_contents contract: hash the ELF header with
 * e_phoff/e_shoff cleared, every program header, then for each final
 * section in shndx order its header with sh_offset cleared and its exact
 * contents (skipping SHT_NOBITS).  The build-ID desc contains zeros while
 * computing this digest.  Hashing the whole file would be incorrect and
 * would make the result depend on symbol/section-table file positions. */
typedef struct MiniLdSha1 {
    uint32_t words[5];
    uint64_t bytes;
    unsigned char pending[64];
    size_t pending_size;
} MiniLdSha1;

static uint32_t minild_sha1_rotate(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static void minild_sha1_block(MiniLdSha1 *sha1,
                              const unsigned char *block) {
    uint32_t expanded[80];
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0U; i < 16U; ++i) {
        const unsigned char *p = block + i * 4U;
        expanded[i] = ((uint32_t)p[0] << 24U) |
                      ((uint32_t)p[1] << 16U) |
                      ((uint32_t)p[2] << 8U) | p[3];
    }
    for (i = 16U; i < 80U; ++i) {
        expanded[i] = minild_sha1_rotate(
            expanded[i - 3U] ^ expanded[i - 8U] ^
            expanded[i - 14U] ^ expanded[i - 16U], 1U);
    }
    a = sha1->words[0];
    b = sha1->words[1];
    c = sha1->words[2];
    d = sha1->words[3];
    e = sha1->words[4];
    for (i = 0U; i < 80U; ++i) {
        uint32_t f, k, t;
        if (i < 20U) {
            f = (b & c) | (~b & d);
            k = UINT32_C(0x5a827999);
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = UINT32_C(0x6ed9eba1);
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = UINT32_C(0x8f1bbcdc);
        } else {
            f = b ^ c ^ d;
            k = UINT32_C(0xca62c1d6);
        }
        t = minild_sha1_rotate(a, 5U) + f + e + k + expanded[i];
        e = d;
        d = c;
        c = minild_sha1_rotate(b, 30U);
        b = a;
        a = t;
    }
    sha1->words[0] += a;
    sha1->words[1] += b;
    sha1->words[2] += c;
    sha1->words[3] += d;
    sha1->words[4] += e;
}

static void minild_sha1_init(MiniLdSha1 *sha1) {
    static const uint32_t initial[5] = {
        UINT32_C(0x67452301), UINT32_C(0xefcdab89),
        UINT32_C(0x98badcfe), UINT32_C(0x10325476),
        UINT32_C(0xc3d2e1f0)
    };
    memset(sha1, 0, sizeof(*sha1));
    memcpy(sha1->words, initial, sizeof(initial));
}

static void minild_sha1_update(MiniLdSha1 *sha1,
                               const unsigned char *data, size_t length) {
    size_t consumed = 0U;

    sha1->bytes += (uint64_t)length;
    while (consumed < length) {
        size_t portion = 64U - sha1->pending_size;
        if (portion > length - consumed) {
            portion = length - consumed;
        }
        memcpy(sha1->pending + sha1->pending_size, data + consumed, portion);
        sha1->pending_size += portion;
        consumed += portion;
        if (sha1->pending_size == 64U) {
            minild_sha1_block(sha1, sha1->pending);
            sha1->pending_size = 0U;
        }
    }
}

static void minild_sha1_finish(MiniLdSha1 *sha1, unsigned char digest[20]) {
    const uint64_t bits = sha1->bytes * UINT64_C(8);
    static const unsigned char first_pad = 0x80U;
    static const unsigned char zeros[64] = {0};
    unsigned char size_bytes[8];
    unsigned i;

    for (i = 0U; i < 8U; ++i) {
        size_bytes[7U - i] = (unsigned char)(bits >> (i * 8U));
    }
    minild_sha1_update(sha1, &first_pad, 1U);
    if (sha1->pending_size > 56U) {
        minild_sha1_update(sha1, zeros, 64U - sha1->pending_size);
    }
    if (sha1->pending_size < 56U) {
        minild_sha1_update(sha1, zeros, 56U - sha1->pending_size);
    }
    minild_sha1_update(sha1, size_bytes, sizeof(size_bytes));
    for (i = 0U; i < 5U; ++i) {
        digest[i * 4U] = (unsigned char)(sha1->words[i] >> 24U);
        digest[i * 4U + 1U] = (unsigned char)(sha1->words[i] >> 16U);
        digest[i * 4U + 2U] = (unsigned char)(sha1->words[i] >> 8U);
        digest[i * 4U + 3U] = (unsigned char)(sha1->words[i]);
    }
}

static bool minild_final_fill_gnu_build_id(
    const MiniLdState *state, const MiniLdOutputPlan *output_plan,
    const MiniLdProgramPlan *program_plan,
    const MiniLdFinalSectionTables *tables,
    unsigned char *image, size_t image_size) {
    const unsigned char elf_class = state->target->elf_class;
    const MiniLdElfAbiSizes abi = minild_elf_abi_sizes(elf_class);
    size_t note_offset = SIZE_MAX;
    size_t i;
    MiniLdSha1 sha1;
    unsigned char header[64];
    unsigned char packed_section[64];
    unsigned char digest[20];

    if (!state->build_id_sha1) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if (section->indexable &&
            strcmp(section->name, ".note.gnu.build-id") == 0) {
            if (note_offset != SIZE_MAX || section->size != 36U ||
                section->type != SHT_NOTE ||
                output_plan->work_shndx[i] == 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-generated-build-id-section\n");
                return false;
            }
            note_offset = output_plan->work_file_offsets[i];
        }
    }
    if (note_offset == SIZE_MAX ||
        !range_ok(note_offset, 36U, image_size) ||
        load_u32le(image + note_offset) != 4U ||
        load_u32le(image + note_offset + 4U) != 20U ||
        load_u32le(image + note_offset + 8U) != 3U ||
        memcmp(image + note_offset + 12U, "GNU", 4U) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: missing-or-corrupt-generated-build-id\n");
        return false;
    }
    memset(image + note_offset + 16U, 0, 20U);
    minild_sha1_init(&sha1);
    memcpy(header, image, abi.ehdr);
    if (elf_class == ELFCLASS64) {
        store_u64le(header + 32U, 0U);
        store_u64le(header + 40U, 0U);
    } else {
        store_u32le(header + 28U, 0U);
        store_u32le(header + 32U, 0U);
    }
    minild_sha1_update(&sha1, header, abi.ehdr);
    if (!range_ok(abi.ehdr, program_plan->count * abi.phdr,
                  image_size)) {
        return false;
    }
    minild_sha1_update(&sha1, image + abi.ehdr,
                       program_plan->count * abi.phdr);
    for (i = 0U; i < tables->count; ++i) {
        const MiniElfSection *section = &tables->headers[i];
        MiniElfSection checksum_header = *section;

        checksum_header.offset = 0U;
        memset(packed_section, 0, sizeof(packed_section));
        if (!minild_final_pack_section_header(
                packed_section, elf_class, &checksum_header)) {
            return false;
        }
        minild_sha1_update(&sha1, packed_section, abi.shdr);
        if (section->type != SHT_NOBITS && section->size != 0U) {
            if (section->size > SIZE_MAX ||
                !range_ok((size_t)section->offset,
                          (size_t)section->size, image_size)) {
                return false;
            }
            minild_sha1_update(&sha1, image + (size_t)section->offset,
                               (size_t)section->size);
        }
    }
    minild_sha1_finish(&sha1, digest);
    memcpy(image + note_offset + 16U, digest, sizeof(digest));
    return true;
}

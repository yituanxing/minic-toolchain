/* Exact original ARM64_ASM_PREAMBLE + __tlbi(op[, arg]) GNU shape. */
#define TLBI_ADDRESS(name) \
    void flush_##name(unsigned long long address) { \
        asm (".arch armv8.5-a\n\t" "tlbi " #name ", %x0\n" \
             : : "rZ" (address)); \
    }
#define TLBI_CONTEXT(name) \
    void flush_##name(void) { \
        asm (".arch armv8.5-a\n\t" "tlbi " #name "\n" : : ); \
    }

TLBI_ADDRESS(vale1is)
TLBI_ADDRESS(vale2is)
TLBI_ADDRESS(aside1is)
TLBI_ADDRESS(rvale1is)
TLBI_ADDRESS(vae1is)
TLBI_ADDRESS(rvae1is)
TLBI_ADDRESS(vaale1is)
TLBI_ADDRESS(vaae1is)
TLBI_ADDRESS(ipas2e1is)
TLBI_ADDRESS(ipas2e1)
TLBI_ADDRESS(vae2is)

TLBI_CONTEXT(vmalle1)
TLBI_CONTEXT(vmalle1is)
TLBI_CONTEXT(alle1is)
TLBI_CONTEXT(vmalls12e1is)

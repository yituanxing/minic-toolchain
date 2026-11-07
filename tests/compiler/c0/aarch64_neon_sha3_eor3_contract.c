/* Preprocess with the host CPP; MiniC's registered AArch64 builtin type
 * supplies the exact 128-bit NEON Q representation without system headers.
 * Negative variants check that only the original GNU EOR3 form survives. */
#if defined(SHA3_BAD_WIDTH)
typedef __Uint8x8_t sha3_vec;
#elif defined(SHA3_BAD_GNU_VECTOR)
typedef unsigned long long sha3_vec __attribute__((vector_size(16)));
#else
typedef __Uint64x2_t sha3_vec;
#endif

#if defined(SHA3_BAD_LANE)
typedef __Uint8x16_t sha3_input_vec;
#else
typedef sha3_vec sha3_input_vec;
#endif

sha3_vec sha3_contract(sha3_vec p, sha3_input_vec q, sha3_vec r) {
    sha3_vec result;
#if defined(SHA3_BAD_TEMPLATE)
    __asm__(".arch_extension sha3\n"
            "eor3 %0.16b, %1.16b, %2.16b, %3.16b\n"
            "nop"
            : "=w"(result) : "w"(p), "w"(q), "w"(r));
#elif defined(SHA3_BAD_VOLATILE)
    __asm__ volatile(".arch_extension sha3\n"
                     "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
                     : "=w"(result) : "w"(p), "w"(q), "w"(r));
#elif defined(SHA3_BAD_CONSTRAINT)
    __asm__(".arch_extension sha3\n"
            "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
            : "=w"(result) : "r"(p), "w"(q), "w"(r));
#elif defined(SHA3_BAD_OUTPUT_CONSTRAINT)
    __asm__(".arch_extension sha3\n"
            "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
            : "=r"(result) : "w"(p), "w"(q), "w"(r));
#else
    __asm__(".arch_extension sha3\n"
            "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
            : "=w"(result) : "w"(p), "w"(q), "w"(r));
#endif
    return result;
}

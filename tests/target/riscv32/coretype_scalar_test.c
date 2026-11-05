#include "target/riscv32/rv32_internal.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) return __LINE__; } while (0)

static bool register_integer(MinicCoreTypeContext *ctx, unsigned bits,
                             bool sign, bool is_bool, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = is_bool ? 1U : bits / 8U;
    type.alignment = type.size;
    type.value.integer.bit_width = bits;
    type.value.integer.is_signed = sign;
    type.value.integer.is_bool = is_bool;
    return minic_core_type_register(ctx, &type, id);
}

static bool register_float(MinicCoreTypeContext *ctx, size_t size,
                           MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = size;
    type.alignment = size;
    type.value.floating.format = size == 4U
        ? MINIC_CORE_FLOAT_BINARY32 : MINIC_CORE_FLOAT_BINARY64;
    return minic_core_type_register(ctx, &type, id);
}

static bool register_pointer(MinicCoreTypeContext *ctx,
                             MinicCoreTypeId pointee, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(ctx, &type, id);
}

static bool check_load_opcode(const MinicCoreFunction *function,
                              MinicCoreTypeId id, const char *expected) {
    FILE *stream = tmpfile();
    char buffer[512];
    size_t count;
    bool ok;
    if (stream == NULL) return false;
    ok = rv32_core_load_mem(stream, function, id, "t1", 1U) &&
         fflush(stream) == 0 && fseek(stream, 0L, SEEK_SET) == 0;
    count = ok ? fread(buffer, 1U, sizeof(buffer) - 1U, stream) : 0U;
    buffer[count] = '\0';
    ok = ok && strstr(buffer, expected) != NULL;
    return fclose(stream) == 0 && ok;
}

static bool check_canon_opcode(const MinicCoreFunction *function,
                               MinicCoreTypeId id, const char *expected) {
    FILE *stream = tmpfile();
    char buffer[256];
    size_t count;
    bool ok;
    if (stream == NULL) return false;
    ok = rv32_core_canon(stream, function, id, "t0") &&
         fflush(stream) == 0 && fseek(stream, 0L, SEEK_SET) == 0;
    count = ok ? fread(buffer, 1U, sizeof(buffer) - 1U, stream) : 0U;
    buffer[count] = '\0';
    ok = ok && strstr(buffer, expected) != NULL;
    return fclose(stream) == 0 && ok;
}

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId boolean, s8, u8, s16, u16, s32, u32, s64, u64;
    MinicCoreTypeId f32, f64, pointer, qualified_pointer, invalid_record;
    MinicCoreTypeId invalid_pointer;
    MinicCoreType qualifier = {0};
    MinicCoreType record = {0};
    MinicCoreType wrong_layout = {0};
    MinicCoreValue scalar_value = {0};
    MinicCoreFunction function = {0};
    unsigned bits, words;
    bool sign;
    FILE *stream;

    minic_core_type_context_initialize(&types);
    REQUIRE(register_integer(&types, 1U, false, true, &boolean));
    REQUIRE(register_integer(&types, 8U, true, false, &s8));
    REQUIRE(register_integer(&types, 8U, false, false, &u8));
    REQUIRE(register_integer(&types, 16U, true, false, &s16));
    REQUIRE(register_integer(&types, 16U, false, false, &u16));
    REQUIRE(register_integer(&types, 32U, true, false, &s32));
    REQUIRE(register_integer(&types, 32U, false, false, &u32));
    REQUIRE(register_integer(&types, 64U, true, false, &s64));
    REQUIRE(register_integer(&types, 64U, false, false, &u64));
    REQUIRE(register_float(&types, 4U, &f32));
    REQUIRE(register_float(&types, 8U, &f64));
    REQUIRE(register_pointer(&types, s32, &pointer));
    qualifier.kind = MINIC_CORE_TYPE_ALIGNED;
    qualifier.has_layout = true;
    qualifier.size = 4U;
    qualifier.alignment = 8U;
    qualifier.has_abi_alignment_override = true;
    qualifier.value.aligned.base = pointer;
    REQUIRE(minic_core_type_register(&types, &qualifier, &qualified_pointer));
    record.kind = MINIC_CORE_TYPE_RECORD;
    record.has_layout = false;
    REQUIRE(minic_core_type_register(&types, &record, &invalid_record));
    wrong_layout.kind = MINIC_CORE_TYPE_POINTER;
    wrong_layout.has_layout = true;
    wrong_layout.size = 8U;
    wrong_layout.alignment = 8U;
    wrong_layout.value.pointer.pointee = s32;
    REQUIRE(minic_core_type_register(&types, &wrong_layout, &invalid_pointer));

    /* Values no longer carry a C descriptor. All scalar classifiers use
     * the sealed Core integer ID and layout. */
    scalar_value.core_type_id = s8;
    function.type_context = &types;
    function.values = &scalar_value;
    function.value_count = 1U;
    REQUIRE(rv32_core_scalar(&function,
                             function.values[0].core_type_id, &bits, &sign));
    REQUIRE(bits == 8U && sign);
    REQUIRE(rv32_core_scalar_storage_words(&function, boolean, &words) && words == 1U);
    REQUIRE(rv32_core_scalar(&function, boolean, &bits, &sign) && bits == 1U && !sign);
    REQUIRE(rv32_core_scalar(&function, u8, &bits, &sign) && bits == 8U && !sign);
    REQUIRE(rv32_core_scalar(&function, s16, &bits, &sign) && bits == 16U && sign);
    REQUIRE(rv32_core_scalar(&function, u16, &bits, &sign) && bits == 16U && !sign);
    REQUIRE(rv32_core_scalar(&function, s32, &bits, &sign) && bits == 32U && sign);
    REQUIRE(rv32_core_scalar(&function, u32, &bits, &sign) && bits == 32U && !sign);
    REQUIRE(rv32_core_scalar(&function, pointer, &bits, &sign) && bits == 32U && !sign);
    REQUIRE(rv32_core_scalar(&function, qualified_pointer, &bits, &sign) &&
            bits == 32U && !sign);
    REQUIRE(rv32_core_scalar(&function, f32, &bits, &sign) && bits == 32U && !sign);
    REQUIRE(!rv32_core_scalar(&function, f64, &bits, &sign));
    REQUIRE(!rv32_core_scalar(&function, u64, &bits, &sign));
    REQUIRE(rv32_core_scalar_storage_words(&function, s64, &words) && words == 2U);
    REQUIRE(rv32_core_scalar_storage_words(&function, u64, &words) && words == 2U);
    REQUIRE(rv32_core_scalar_storage_words(&function, f64, &words) && words == 2U);
    REQUIRE(rv32_core_scalar_storage_words(&function, pointer, &words) && words == 1U);
    REQUIRE(!rv32_core_scalar_storage_words(&function, invalid_record, &words));
    REQUIRE(rv32_core_is_wide_integer(&function, s64));
    REQUIRE(rv32_core_is_wide_integer(&function, u64));
    REQUIRE(!rv32_core_is_wide_integer(&function, f64));
    REQUIRE(rv32_core_unsigned_integer(&function, u64));
    REQUIRE(!rv32_core_unsigned_integer(&function, s64));
    REQUIRE(rv32_core_integer_type(&function, boolean));
    REQUIRE(!rv32_core_integer_type(&function, pointer));
    REQUIRE(rv32_core_pointer_type(&function, qualified_pointer));
    REQUIRE(rv32_core_pointee_type(&function, pointer) == s32);
    REQUIRE(!rv32_core_pointer_type(&function, invalid_pointer));
    REQUIRE(rv32_core_pointee_type(&function, invalid_pointer) ==
            MINIC_CORE_TYPE_INVALID);
    REQUIRE(rv32_core_pointee_type(&function, s32) == MINIC_CORE_TYPE_INVALID);
    REQUIRE(check_load_opcode(&function, s8, "  lb t1"));
    REQUIRE(check_load_opcode(&function, u8, "  lbu t1"));
    REQUIRE(check_canon_opcode(&function, boolean, "snez"));
    REQUIRE(check_canon_opcode(&function, s16, "srai"));

    /* Detached Core is rejected even with a plausible Core integer ID. */
    function.type_context = NULL;
    REQUIRE(!rv32_core_scalar(&function, s8, &bits, &sign));
    REQUIRE(!rv32_core_scalar_storage_words(&function, u64, &words));
    REQUIRE(!rv32_core_is_wide_integer(&function, u64));
    REQUIRE(!rv32_core_unsigned_integer(&function, u64));
    REQUIRE(!rv32_core_integer_type(&function, u64));
    REQUIRE(!rv32_core_pointer_type(&function, pointer));
    REQUIRE(rv32_core_pointee_type(&function, pointer) == MINIC_CORE_TYPE_INVALID);
    stream = tmpfile();
    REQUIRE(stream != NULL);
    REQUIRE(!rv32_core_canon(stream, &function, s8, "t0"));
    REQUIRE(!rv32_core_load_mem(stream, &function, s8, "t1", 1U));
    REQUIRE(!rv32_core_store_mem(stream, &function, s8, 1U));
    REQUIRE(ftell(stream) == 0L);
    REQUIRE(fclose(stream) == 0);

    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    int failed = run();
    if (failed != 0) {
        (void)fprintf(stderr, "rv32_coretype_scalar_test failed at line %d\n", failed);
        return 1;
    }
    (void)puts("RV32_CORETYPE_SCALAR=PASS");
    return 0;
}

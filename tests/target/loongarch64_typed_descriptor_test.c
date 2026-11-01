/* Loong Linux descriptor registry: source-ordered typed ID, independently
 * checked GNU template, zero-only variant, and fail-closed forged metadata.
 * These IDs deliberately follow the single source/machine descriptor table. */
#include "target/loongarch64/asm_contract.h"

/* This is an assertion-based machine-contract test: release -DNDEBUG must
 * not silently disable its forged-Core and no-partial-output checks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool gate(uint32_t id, uint32_t variant, const char *literal) {
    return minic_loongarch64_typed_descriptor_matches_template(
        id, variant, literal, literal ? strlen(literal) : 0U);
}

static void machine_reject(uint32_t id, uint32_t variant, const char *literal) {
    char text[256];
    MinicCoreInlineAsm descriptor = {0};
    MinicCoreFunction function = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicCoreInstruction ins = {0};
    FILE *out = tmpfile();
    assert(out != NULL);
    assert(strlen(literal) + 1U <= sizeof(text));
    memcpy(text, literal, strlen(literal) + 1U);
    descriptor.template_text = text;
    descriptor.template_length = strlen(literal);
    descriptor.target_machine_form_id = id;
    descriptor.target_machine_form_variant = variant;
    function.inline_asms = &descriptor;
    function.inline_asm_count = 1U;
    ctx.fn = &function;
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.type = minic_type_void();
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    assert(!minic_loongarch64_emit_qualified_structured_asm(out, &ctx, &ins));
    assert(ftell(out) == 0L);
    fclose(out);
}

#define CHECK_FORM(id, literal, corrupt) do { \
    assert(gate(id, 0U, literal)); \
    assert(minic_loongarch64_descriptor_template_requires_typed_id( \
        literal, strlen(literal))); \
    assert(!gate(id, 1U, literal)); \
    assert(!gate(id, 0U, corrupt)); \
    assert(!minic_loongarch64_descriptor_template_requires_typed_id( \
        corrupt, strlen(corrupt))); \
    assert(!gate(0U, 0U, literal)); \
    assert(!gate(15U, 0U, literal)); \
} while (0)

int main(void) {
    CHECK_FORM(1U, "cacop %0, %1", "cacop %0, %1; break 0");
    CHECK_FORM(2U, "syscall 0", "syscall 0; break 0");
    CHECK_FORM(5U, "cpucfg %0, %1", "cpucfg %0, %1; break 0");
    CHECK_FORM(13U, "sltu %0,%1,%2\n\tsub.d %0,$zero,%0",
        "sltu %0,%1,%2\n\tsub.d %0,$zero,%0; break 0");
    CHECK_FORM(14U,
        "1: ll.d %1,%0\n\tbnez %1,2f\n\tor %1,%1,%2\n"
        "\tsc.d %1,%0\n\tbeqz %1,1b\n\tnop\n2: dbar 0x700",
        "1: ll.d %1,%0\n\tbnez %1,2f\n\tor %1,%1,%2\n"
        "\tsc.d %1,%0\n\tbeqz %1,1b\n\tnop\n2: dbar 0x700; break 0");
    assert(!gate(1U, 0U, "syscall 0"));
    assert(!gate(5U, 0U, "cacop %0, %1"));
    /* A forged descriptor ID must reject even unrelated SIMD-like text;
     * the typed path runs before the target's SIMD fallback. */
    assert(!gate(1U, 0U, "xvadd.b $xr0, $xr1, $xr2"));
    assert(!gate(2U, 0U, NULL));
    machine_reject(0U, 0U, "cacop %0, %1");
    machine_reject(0U, 0U, "syscall 0");
    machine_reject(1U, 0U, "syscall 0");
    machine_reject(1U, 1U, "cacop %0, %1");
    machine_reject(15U, 0U, "cacop %0, %1");
    machine_reject(1U, 0U, "xvadd.b $xr0, $xr1, $xr2");
    puts("LOONG_TYPED_DESCRIPTOR_GATE=PASS exact=5 zero_id_reserved=5 forged_id=5 forged_variant=5 corrupted_template=5 cross_id=3 null=1 machine_reject=6 partial_s=0");
    return 0;
}

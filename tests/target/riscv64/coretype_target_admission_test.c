#include "core/core_ir.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/record_snapshot.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_type_query.h"
#include "target/riscv64/isa.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

static bool register_type(MinicCoreTypeContext *ctx, MinicCoreTypeKind kind,
                          size_t size, size_t alignment, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = kind;
    type.has_layout = kind != MINIC_CORE_TYPE_VOID;
    type.size = size;
    type.alignment = alignment;
    if (kind == MINIC_CORE_TYPE_INTEGER) {
        type.value.integer.bit_width = (unsigned int)(size * 8U);
        type.value.integer.is_signed = true;
    }
    if (kind == MINIC_CORE_TYPE_RECORD)
        type.value.record.is_complete = true;
    return minic_core_type_register(ctx, &type, id);
}

static bool register_pointer(MinicCoreTypeContext *ctx, MinicCoreTypeId pointee,
                             MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(ctx, &type, id);
}

static bool register_overflow_scalar(MinicCoreTypeContext *ctx, size_t size,
                                     bool is_signed, bool is_bool,
                                     MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = size;
    type.alignment = size;
    type.value.integer.bit_width = (unsigned int)(size * 8U);
    type.value.integer.is_signed = is_signed;
    type.value.integer.is_bool = is_bool;
    return minic_core_type_register(ctx, &type, id);
}

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId void_id, int_id, wide_id, record_id, pointer_id, record_pointer_id;
    MinicCoreTypeId volatile_record_id, volatile_record_pointer_id;
    MinicCoreTypeId bool_id, unsigned_id;
    MinicCoreTypeId malformed_pointer_id, other_record_id, direct_signature_id;
    MinicCoreType qualifier = {0};
    MinicCoreValue values[4];
    MinicCoreObject objects[1];
    MinicCoreFunction function = {0};
    MinicCoreInstruction ins = {0};
    MinicCoreInlineAsm asm_binding = {0};
    MinicCoreFixedRegisterBinding fixed_binding = {0};
    MinicCoreCallee callee = {0};
    MinicCoreCallArgument call_argument = {0};
    MinicCoreCallSite call_site = {0};
    MinicCoreCallIndex call_index = {0};
    MinicRiscv64CallPlanCache call_cache = {0};
    MinicRiscvIsa strict_isa = {64U, MINIC_RISCV_ABI_LP64, 0U, 0U, true};
    MinicCoreInstruction isa_ins = {0};
    char reason[256];
    char output[2048];
    size_t record_size;
    FILE *stream;
    size_t written;

    minic_core_type_context_initialize(&types);
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_VOID, 0U, 0U, &void_id));
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_INTEGER, 4U, 4U, &int_id));
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_INTEGER, 16U, 16U, &wide_id));
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_RECORD, 4U, 4U, &record_id));
    REQUIRE(register_pointer(&types, int_id, &pointer_id));
    REQUIRE(register_pointer(&types, record_id, &record_pointer_id));
    REQUIRE(register_overflow_scalar(&types, 1U, false, true, &bool_id));
    REQUIRE(register_overflow_scalar(&types, 4U, false, false, &unsigned_id));
    qualifier.kind = MINIC_CORE_TYPE_QUALIFIED;
    qualifier.has_layout = true;
    qualifier.size = 4U;
    qualifier.alignment = 4U;
    qualifier.is_volatile = true;
    qualifier.value.qualified.base = record_id;
    REQUIRE(minic_core_type_register(&types, &qualifier, &volatile_record_id));
    REQUIRE(register_pointer(&types, volatile_record_id, &volatile_record_pointer_id));
    {
        MinicCoreType malformed = {0};
        malformed.kind = MINIC_CORE_TYPE_POINTER;
        malformed.has_layout = true;
        malformed.size = 4U; /* valid Core entry but not an RV64 pointer */
        malformed.alignment = 4U;
        malformed.value.pointer.pointee = int_id;
        REQUIRE(minic_core_type_register(&types, &malformed, &malformed_pointer_id));
    }

    memset(values, 0, sizeof(values));
    memset(objects, 0, sizeof(objects));

    /* All legacy C descriptors deliberately remain zero/void. If the RV64
     * target reopens them, these valid CoreType cases must fail. */
    function.type_context = &types;
    function.values = values;
    function.value_count = 4U;
    function.objects = objects;
    function.object_count = 1U;
    objects[0].core_type_id = record_id;
    values[0].core_type_id = record_pointer_id;
    values[1].core_type_id = volatile_record_pointer_id;
    values[2].core_type_id = pointer_id;
    values[3].core_type_id = int_id;

    /* Checked arithmetic is one CoreType-only admission+emission family:
     * all C descriptors are deliberately void, including the bool result,
     * operand signedness and pointer destination type. */
    {
        MinicCoreTypeId result_id = MINIC_CORE_TYPE_INVALID;
        size_t result_bytes = 0U;
        bool result_unsigned = true;
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
        ins.core_type_id = bool_id;
        ins.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_ADD;
        ins.value.integer_overflow.left = 3U;
        ins.value.integer_overflow.right = 1U;
        ins.value.integer_overflow.result_address = 2U;
        values[1].core_type_id = int_id;
        REQUIRE(minic_riscv64_core_integer_overflow_supported(
            &function, &ins, &result_id, &result_bytes, &result_unsigned));
        REQUIRE(result_id == int_id && result_bytes == 4U && !result_unsigned);
        values[1].core_type_id = unsigned_id;
        REQUIRE(minic_riscv64_core_integer_overflow_supported(
            &function, &ins, &result_id, &result_bytes, &result_unsigned));
        REQUIRE(result_id == int_id && result_bytes == 4U);
        values[1].core_type_id = wide_id;
        REQUIRE(!minic_riscv64_core_integer_overflow_supported(
            &function, &ins, &result_id, &result_bytes, &result_unsigned));
        values[1].core_type_id = int_id;
        ins.core_type_id = int_id;
        REQUIRE(!minic_riscv64_core_integer_overflow_supported(
            &function, &ins, &result_id, &result_bytes, &result_unsigned));
        ins.core_type_id = bool_id;
        function.type_context = NULL;
        REQUIRE(!minic_riscv64_core_integer_overflow_supported(
            &function, &ins, &result_id, &result_bytes, &result_unsigned));
        function.type_context = &types;
    }

    values[0].core_type_id = record_pointer_id;
    values[1].core_type_id = volatile_record_pointer_id;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_load.source_address = 0U;
    ins.value.record_load.destination_object = 0U;
    REQUIRE(minic_riscv64_core_record_load_supported(&function, &ins, &record_size));
    REQUIRE(record_size == 4U);
    ins.value.record_load.source_address = 1U;
    REQUIRE(!minic_riscv64_core_record_load_supported(&function, &ins, NULL));
    ins.value.record_load.is_volatile = true;
    REQUIRE(minic_riscv64_core_record_load_supported(&function, &ins, NULL));
    ins.value.record_load.destination_object = 1U;
    REQUIRE(!minic_riscv64_core_record_load_supported(&function, &ins, NULL));

    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_copy.destination_address = 0U;
    ins.value.record_copy.source_address = 1U;
    REQUIRE(minic_riscv64_core_record_copy_supported(&function, &ins));
    ins.value.record_copy.source_address = 2U;
    REQUIRE(!minic_riscv64_core_record_copy_supported(&function, &ins));

    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    ins.core_type_id = int_id;
    /* Shared Core shape now checks the result SSA ID as well as operands. */
    ins.result = 3U;
    ins.value.operand = 2U;
    REQUIRE(minic_riscv64_core_scalar_bitcast_supported(&function, &ins));
    ins.value.operand = 3U;
    REQUIRE(!minic_riscv64_core_scalar_bitcast_supported(&function, &ins));

    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS;
    ins.core_type_id = pointer_id;
    ins.value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
    REQUIRE(!minic_riscv64_core_call_frame_address_supported(&function, &ins));
    REQUIRE(register_pointer(&types, void_id, &pointer_id));
    ins.core_type_id = pointer_id;
    REQUIRE(minic_riscv64_core_call_frame_address_supported(&function, &ins));
    ins.value.call_frame_address.level = 1U;
    REQUIRE(!minic_riscv64_core_call_frame_address_supported(&function, &ins));

    asm_binding.template_text = "add %0, %0, zero";
    asm_binding.template_length = strlen(asm_binding.template_text);
    asm_binding.is_volatile = true;
    function.inline_asms = &asm_binding;
    function.inline_asm_count = 1U;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM;
    ins.core_type_id = int_id;
    ins.value.inline_asm_id = 0U;
    REQUIRE(minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    ins.core_type_id = record_id;
    REQUIRE(!minic_riscv64_inline_asm_instruction_supported(&function, &ins));

    /* Fixed-register register-output asm must follow the Core pointee and
     * Core binding identity even when both source descriptors are void.
     * A malformed pointer layout must fail closed before address transport. */
    fixed_binding.register_name = "a2";
    fixed_binding.register_name_length = 2U;
    fixed_binding.core_type_id = int_id;
    fixed_binding.is_local = true;
    function.fixed_register_bindings = &fixed_binding;
    function.fixed_register_binding_count = 1U;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.core_type_id = void_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    ins.value.structured_inline_asm.operand_count = 1U;
    ins.value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ins.value.structured_inline_asm.operands[0].operand_index = 0U;
    ins.value.structured_inline_asm.operands[0].value = 2U;
    ins.value.structured_inline_asm.operands[0].has_fixed_register_binding = true;
    values[2].core_type_id = malformed_pointer_id;
    REQUIRE(!minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    REQUIRE(register_pointer(&types, wide_id, &values[2].core_type_id));
    REQUIRE(!minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    values[2].core_type_id = pointer_id;
    /* pointer_id now points to void after the CALL_FRAME_ADDRESS test;
     * restore the original int-pointer identity for structured asm. */
    REQUIRE(register_pointer(&types, int_id, &values[2].core_type_id));
    REQUIRE(minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    fixed_binding.core_type_id = record_id;
    REQUIRE(!minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    fixed_binding.core_type_id = int_id;
    function.type_context = NULL;
    REQUIRE(!minic_riscv64_inline_asm_instruction_supported(&function, &ins));
    function.type_context = &types;

    /* A source-C descriptor cannot license snapshot stack reuse. Different
     * Core Record IDs (even with identical layouts) must remain distinct. */
    {
        MinicCoreType other_record = {0};
        MinicCoreObject snapshot_objects[2];
        MinicCoreInstruction snapshot_instructions[2] = {{0}, {0}};
        MinicCoreInstructionId snapshot_order[] = {0U, 1U};
        MinicCoreCallArgument snapshot_argument = {0};
        MinicCoreBlock snapshot_block = {0};
        MinicCoreFunction snapshot_function = {0};
        MinicCoreObjectId candidate = MINIC_CORE_OBJECT_INVALID;
        memset(snapshot_objects, 0, sizeof(snapshot_objects));
        other_record.kind = MINIC_CORE_TYPE_RECORD;
        other_record.has_layout = true;
        other_record.size = 4U;
        other_record.alignment = 4U;
        other_record.value.record.is_complete = true;
        other_record.value.record.is_union = true;
        REQUIRE(minic_core_type_register(&types, &other_record, &other_record_id));
        REQUIRE(other_record_id != record_id);
        snapshot_objects[0].core_type_id = record_id;
        snapshot_objects[1].core_type_id = record_id;
        snapshot_instructions[0].kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
        snapshot_instructions[0].value.record_load.destination_object = 0U;
        snapshot_instructions[1].kind = MINIC_CORE_INSTRUCTION_CALL;
        snapshot_instructions[1].value.call.argument_count = 1U;
        snapshot_instructions[1].value.call.result_object = 1U;
        snapshot_argument.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
        snapshot_argument.value.object_id = 0U;
        snapshot_block.instructions = snapshot_order;
        snapshot_block.instruction_count = 2U;
        snapshot_function.type_context = &types;
        snapshot_function.objects = snapshot_objects;
        snapshot_function.object_count = 2U;
        snapshot_function.instructions = snapshot_instructions;
        snapshot_function.instruction_count = 2U;
        snapshot_function.blocks = &snapshot_block;
        snapshot_function.block_count = 1U;
        snapshot_function.call_arguments = &snapshot_argument;
        snapshot_function.call_argument_count = 1U;
        REQUIRE(minic_riscv64_record_call_result_snapshot_reuse_candidate(
            &snapshot_function, 1U, &candidate) && candidate == 0U);
        snapshot_objects[1].core_type_id = other_record_id;
        REQUIRE(!minic_riscv64_record_call_result_snapshot_reuse_candidate(
            &snapshot_function, 1U, &candidate));
        snapshot_objects[1].core_type_id = volatile_record_id;
        REQUIRE(!minic_riscv64_record_call_result_snapshot_reuse_candidate(
            &snapshot_function, 1U, &candidate));
        snapshot_function.type_context = NULL;
        REQUIRE(!minic_riscv64_record_call_result_snapshot_reuse_candidate(
            &snapshot_function, 1U, &candidate));
    }

    /* The prepared CALL ABI must not consult the C signature or the C
     * argument type. Its authoritative fixed prefix is the CoreTypeId. */
    {
        MinicCoreType direct_signature = {0};
        direct_signature.kind = MINIC_CORE_TYPE_FUNCTION;
        direct_signature.value.function.return_type = int_id;
        direct_signature.value.function.parameters = &int_id;
        direct_signature.value.function.parameter_count = 1U;
        REQUIRE(minic_core_type_register(&types, &direct_signature,
                                         &direct_signature_id));
    }
    callee.name = "core_target_callee";
    callee.name_length = strlen(callee.name);
    callee.core_function_type_id = direct_signature_id;
    call_argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    call_argument.value.value_id = 3U;
    function.call_arguments = &call_argument;
    function.call_argument_count = 1U;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = int_id;
    ins.value.call.argument_begin = 0U;
    ins.value.call.argument_count = 1U;
    function.instructions = &ins;
    function.instruction_count = 1U;
    call_site.instruction_id = 0U;
    call_site.view.callee = &callee;
    call_site.view.arguments = &call_argument;
    call_site.view.core_function_type_id = direct_signature_id;
    call_site.view.core_return_type_id = int_id;
    call_site.view.core_parameter_type_ids = &int_id;
    call_site.view.argument_count = 1U;
    call_site.view.parameter_count = 1U;
    call_site.view.return_form = MINIC_CORE_CALL_RETURN_VALUE;
    call_index.sites = &call_site;
    call_index.count = 1U;
    REQUIRE(minic_riscv64_call_plan_cache_build(
        &function, false, &call_index, &call_cache));
    REQUIRE(call_cache.count == 1U && call_cache.sites != NULL &&
            call_cache.sites[0] != NULL);
    REQUIRE(minic_riscv64_core_direct_call_supported(
        true, &function, &ins, &call_site.view, call_cache.sites[0]));
    /* A wrong machine location must still fail, even with valid Core types. */
    call_cache.sites[0]->arguments[0].integer_register_count = 2U;
    REQUIRE(!minic_riscv64_core_direct_call_supported(
        true, &function, &ins, &call_site.view, call_cache.sites[0]));
    minic_riscv64_call_plan_cache_destroy(&call_cache);

    /* Native instruction and bytewise fallback both derive from Core layout.
     * We check the exact machine transport without relying on GNU cross-as. */
    stream = tmpfile();
    REQUIRE(stream != NULL);
    REQUIRE(minic_riscv64_emit_safe_core_scalar_load(
        stream, &types, int_id, "t0", "t1", "t2", 1U));
    REQUIRE(minic_riscv64_emit_safe_core_scalar_store(
        stream, &types, int_id, "t0", "t1", "t2", 1U));
    REQUIRE(!minic_riscv64_emit_safe_core_scalar_load(
        stream, &types, record_id, "t0", "t1", "t2", 1U));
    rewind(stream);
    written = fread(output, 1U, sizeof(output) - 1U, stream);
    output[written] = '\0';
    REQUIRE(strstr(output, "lbu t2") != NULL);
    REQUIRE(strstr(output, "sb t2") != NULL);
    REQUIRE(strstr(output, "lw t0, 0(t1)") != NULL);
    REQUIRE(strstr(output, "sw t0, 0(t1)") != NULL);
    REQUIRE(fclose(stream) == 0);

    /* Strict ISA admission and frame predicates depend exclusively on sealed
     * CoreType identities. No source-language type field exists on Core IR. */
    REQUIRE(minic_riscv64_core_is_record(&function, record_id));
    REQUIRE(!minic_riscv64_core_is_record(&function, int_id));
    REQUIRE(minic_riscv64_core_is_i128(&function, wide_id));
    REQUIRE(!minic_riscv64_core_is_i128(&function, int_id));
    isa_ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CLZ;
    isa_ins.core_type_id = int_id;
    REQUIRE(minic_riscv_isa_admit_core_instruction(
        &strict_isa, &function, &isa_ins, reason, sizeof(reason)));
    isa_ins.core_type_id = wide_id;
    REQUIRE(!minic_riscv_isa_admit_core_instruction(
        &strict_isa, &function, &isa_ins, reason, sizeof(reason)));
    REQUIRE(strstr(reason, "__int128") != NULL);
    isa_ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    isa_ins.core_type_id = int_id;
    isa_ins.value.operand = 0U;
    values[0].core_type_id = wide_id;
    REQUIRE(!minic_riscv_isa_admit_core_instruction(
        &strict_isa, &function, &isa_ins, reason, sizeof(reason)));
    REQUIRE(strstr(reason, "__int128") != NULL);
    values[0].core_type_id = int_id;
    REQUIRE(minic_riscv_isa_admit_core_instruction(
        &strict_isa, &function, &isa_ins, reason, sizeof(reason)));
    function.type_context = NULL;
    REQUIRE(!minic_riscv_isa_admit_core_instruction(
        &strict_isa, &function, &isa_ins, reason, sizeof(reason)));
    REQUIRE(strstr(reason, "sealed Core") != NULL);

    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    const int line = run();
    if (line != 0) {
        fprintf(stderr, "RV64_CORETYPE_TARGET_ADMISSION=FAIL line=%d\n", line);
        return 1;
    }
    puts("RV64_CORETYPE_TARGET_ADMISSION=PASS record_load_copy volatile bitcast asm fixed_register_pointer_layout snapshot_nominal call_abi core_only_scalar_transport overflow_poison_C strict_isa_frame_type");
    return 0;
}

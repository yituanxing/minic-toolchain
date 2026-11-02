#include "core/core_ir.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/codegen_internal.h"

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

static int run(void) {
    MinicCoreTypeContext types;
    MinicCoreTypeId void_id, int_id, record_id, pointer_id, record_pointer_id;
    MinicCoreTypeId volatile_record_id, volatile_record_pointer_id;
    MinicCoreType qualifier = {0};
    MinicCoreValue values[4];
    MinicCoreObject objects[1];
    MinicCoreFunction function = {0};
    MinicCoreInstruction ins = {0};
    MinicCoreInlineAsm asm_binding = {0};
    MinicC0Program source_program = {0};
    MinicCoreCallee callee = {0};
    MinicCoreCallArgument call_argument = {0};
    MinicCoreCallSite call_site = {0};
    MinicCoreCallIndex call_index = {0};
    MinicRiscv64CallPlanCache call_cache = {0};
    char output[2048];
    size_t record_size;
    FILE *stream;
    size_t written;

    minic_core_type_context_initialize(&types);
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_VOID, 0U, 0U, &void_id));
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_INTEGER, 4U, 4U, &int_id));
    REQUIRE(register_type(&types, MINIC_CORE_TYPE_RECORD, 4U, 4U, &record_id));
    REQUIRE(register_pointer(&types, int_id, &pointer_id));
    REQUIRE(register_pointer(&types, record_id, &record_pointer_id));
    qualifier.kind = MINIC_CORE_TYPE_QUALIFIED;
    qualifier.has_layout = true;
    qualifier.size = 4U;
    qualifier.alignment = 4U;
    qualifier.is_volatile = true;
    qualifier.value.qualified.base = record_id;
    REQUIRE(minic_core_type_register(&types, &qualifier, &volatile_record_id));
    REQUIRE(register_pointer(&types, volatile_record_id, &volatile_record_pointer_id));

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

    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_load.source_address = 0U;
    ins.value.record_load.destination_object = 0U;
    REQUIRE(minic_riscv64_core_record_load_supported(NULL, &function, &ins, &record_size));
    REQUIRE(record_size == 4U);
    ins.value.record_load.source_address = 1U;
    REQUIRE(!minic_riscv64_core_record_load_supported(NULL, &function, &ins, NULL));
    ins.value.record_load.is_volatile = true;
    REQUIRE(minic_riscv64_core_record_load_supported(NULL, &function, &ins, NULL));
    ins.value.record_load.destination_object = 1U;
    REQUIRE(!minic_riscv64_core_record_load_supported(NULL, &function, &ins, NULL));

    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    ins.core_type_id = record_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_copy.destination_address = 0U;
    ins.value.record_copy.source_address = 1U;
    REQUIRE(minic_riscv64_core_record_copy_supported(NULL, &function, &ins));
    ins.value.record_copy.source_address = 2U;
    REQUIRE(!minic_riscv64_core_record_copy_supported(NULL, &function, &ins));

    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;
    ins.core_type_id = int_id;
    ins.value.operand = 2U;
    REQUIRE(minic_riscv64_core_scalar_bitcast_supported(NULL, &function, &ins));
    ins.value.operand = 3U;
    REQUIRE(!minic_riscv64_core_scalar_bitcast_supported(NULL, &function, &ins));

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

    /* The prepared CALL ABI must not consult the C signature or the C
     * argument type. Its authoritative fixed prefix is the CoreTypeId. */
    callee.name = "core_target_callee";
    callee.name_length = strlen(callee.name);
    callee.core_return_type_id = int_id;
    callee.core_parameter_type_ids = &int_id;
    callee.parameter_count = 1U;
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
    call_site.view.core_return_type_id = int_id;
    call_site.view.core_parameter_type_ids = &int_id;
    call_site.view.argument_count = 1U;
    call_site.view.parameter_count = 1U;
    call_site.view.return_form = MINIC_CORE_CALL_RETURN_VALUE;
    call_index.sites = &call_site;
    call_index.count = 1U;
    REQUIRE(minic_riscv64_call_plan_cache_build(
        &source_program, &function, false, &call_index, &call_cache));
    REQUIRE(call_cache.count == 1U && call_cache.sites != NULL &&
            call_cache.sites[0] != NULL);
    REQUIRE(minic_riscv64_core_direct_call_supported(
        &source_program, &function, &ins, &call_site.view, call_cache.sites[0]));
    /* A wrong machine location must still fail, even with valid Core types. */
    call_cache.sites[0]->arguments[0].integer_register_count = 2U;
    REQUIRE(!minic_riscv64_core_direct_call_supported(
        &source_program, &function, &ins, &call_site.view, call_cache.sites[0]));
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
    minic_core_type_context_destroy(&types);
    return 0;
}

int main(void) {
    const int line = run();
    if (line != 0) {
        fprintf(stderr, "RV64_CORETYPE_TARGET_ADMISSION=FAIL line=%d\n", line);
        return 1;
    }
    puts("RV64_CORETYPE_TARGET_ADMISSION=PASS record_load_copy volatile bitcast asm call_abi core_only_scalar_transport");
    return 0;
}

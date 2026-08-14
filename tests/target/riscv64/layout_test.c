#include "frontend/ast.h"
#include "frontend/type.h"
#include "target/data_layout.h"
#include "target/riscv64/layout.h"
#include "target/target_info.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL target/riscv64/layout: %s\n", message);
    return 1;
}

int main(void)
{
    MinicC0Program program;
    MinicBlockId block_id;
    MinicFunctionId function_id;
    MinicLocalId local_id;
    MinicRecordId record_id;
    MinicRecordId floating_record_id;
    MinicRecordId hooks_record_id;
    MinicLocal local;
    MinicType byte_type;
    MinicType pointer_type;
    MinicType record_type;
    MinicType void_pointer_type;
    MinicType alloc_parameter_types[1];
    MinicType free_parameter_types[1];
    MinicType alloc_function_type;
    MinicType free_function_type;
    MinicType alloc_pointer_type;
    MinicType free_pointer_type;
    MinicDiagnostic diagnostic;
    const MinicFunction *function;
    const MinicRecord *record;
    const MinicRecord *floating_record;
    const MinicRecord *hooks_record;
    size_t byte_size;
    size_t byte_alignment;
    size_t long_size;
    size_t long_alignment;
    size_t float_size;
    size_t float_alignment;
    size_t double_size;
    size_t double_alignment;
    size_t function_pointer_size;
    size_t function_pointer_alignment;

    minic_c0_program_initialize(&program);
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    byte_type = minic_type_unsigned_char();

    {
        const MinicTargetInfo *target;
        MinicIntegerSign plain_char_sign;
        MinicType promoted_type;
        MinicType common_type;
        MinicType literal_type;
        unsigned int semantic_bits;

        target = minic_default_target_info();
        if (target == NULL || minic_target_info_integer_model(target) == NULL ||
            !minic_target_info_plain_char_sign(target, &plain_char_sign) ||
            plain_char_sign != MINIC_INTEGER_SIGN_UNSIGNED ||
            !minic_target_info_integer_width(target, &program, minic_type_bool(), &semantic_bits) ||
            semantic_bits != 1U ||
            !minic_target_info_integer_width(target, &program, minic_type_char(), &semantic_bits) ||
            semantic_bits != 8U ||
            !minic_target_info_integer_width(target, &program, minic_type_short(), &semantic_bits) ||
            semantic_bits != 16U ||
            !minic_target_info_integer_width(target, &program, minic_type_int(), &semantic_bits) ||
            semantic_bits != 32U ||
            !minic_target_info_integer_width(target, &program, minic_type_long(), &semantic_bits) ||
            semantic_bits != 64U ||
            !minic_target_info_integer_width(
                target, &program, minic_type_long_long(), &semantic_bits) ||
            semantic_bits != 64U ||
            !minic_target_info_integer_width(target, &program, minic_type_int128(), &semantic_bits) ||
            semantic_bits != 128U) {
            minic_c0_program_destroy(&program);
            return fail("RV64 target integer semantic model");
        }

        if (!minic_target_info_integer_promotion(
                target, minic_type_unsigned_char(), &promoted_type) ||
            !minic_type_equal(promoted_type, minic_type_int()) ||
            !minic_target_info_integer_promotion(
                target, minic_type_unsigned_short(), &promoted_type) ||
            !minic_type_equal(promoted_type, minic_type_int()) ||
            !minic_target_info_integer_promotion(
                target, minic_type_unsigned_int(), &promoted_type) ||
            !minic_type_equal(promoted_type, minic_type_unsigned_int())) {
            minic_c0_program_destroy(&program);
            return fail("RV64 target integer promotion");
        }

        if (!minic_target_info_integer_common(
                target, minic_type_unsigned_int(), minic_type_long(), &common_type) ||
            !minic_type_equal(common_type, minic_type_long()) ||
            !minic_target_info_integer_common(
                target, minic_type_long(), minic_type_unsigned_long(), &common_type) ||
            !minic_type_equal(common_type, minic_type_unsigned_long()) ||
            !minic_target_info_integer_common(target,
                                              minic_type_unsigned_long_long(),
                                              minic_type_int128(),
                                              &common_type) ||
            !minic_type_equal(common_type, minic_type_int128())) {
            minic_c0_program_destroy(&program);
            return fail("RV64 usual arithmetic conversions");
        }

        if (!minic_target_info_integer_literal_type(target,
                                                    MINIC_INTEGER_LITERAL_BASE_DECIMAL,
                                                    false,
                                                    0U,
                                                    UINT64_C(2147483647),
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_int()) ||
            !minic_target_info_integer_literal_type(target,
                                                    MINIC_INTEGER_LITERAL_BASE_DECIMAL,
                                                    false,
                                                    0U,
                                                    UINT64_C(2147483648),
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_long()) ||
            !minic_target_info_integer_literal_type(target,
                                                    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL,
                                                    false,
                                                    0U,
                                                    UINT32_MAX,
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_unsigned_int()) ||
            !minic_target_info_integer_literal_type(target,
                                                    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL,
                                                    false,
                                                    0U,
                                                    UINT64_MAX,
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_unsigned_long()) ||
            minic_target_info_integer_literal_type(target,
                                                   MINIC_INTEGER_LITERAL_BASE_DECIMAL,
                                                   false,
                                                   0U,
                                                   UINT64_MAX,
                                                   &literal_type) ||
            !minic_target_info_integer_literal_type(target,
                                                    MINIC_INTEGER_LITERAL_BASE_DECIMAL,
                                                    true,
                                                    0U,
                                                    UINT64_MAX,
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_unsigned_long())) {
            minic_c0_program_destroy(&program);
            return fail("RV64 integer literal candidate selection");
        }
    }

    {
        MinicTargetIntegerModel model;
        MinicTargetInfo target;
        MinicIntegerSign plain_char_sign;
        MinicType promoted_type;
        MinicType common_type;
        MinicType literal_type;

        (void)memset(&model, 0, sizeof(model));
        (void)memset(&target, 0, sizeof(target));
        model.plain_char_sign = MINIC_INTEGER_SIGN_SIGNED;
        model.semantic_width_bits[MINIC_INTEGER_RANK_BOOL] = 1U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_CHAR] = 8U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_SHORT] = 16U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_INT] = 16U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_LONG] = 32U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_LONG_LONG] = 64U;
        model.semantic_width_bits[MINIC_INTEGER_RANK_INT128] = 128U;
        target.integer_model = &model;

        if (!minic_target_info_plain_char_sign(&target, &plain_char_sign) ||
            plain_char_sign != MINIC_INTEGER_SIGN_SIGNED ||
            !minic_target_info_integer_promotion(&target, minic_type_short(), &promoted_type) ||
            !minic_type_equal(promoted_type, minic_type_int()) ||
            !minic_target_info_integer_promotion(
                &target, minic_type_unsigned_short(), &promoted_type) ||
            !minic_type_equal(promoted_type, minic_type_unsigned_int()) ||
            !minic_target_info_integer_common(
                &target, minic_type_unsigned_int(), minic_type_long(), &common_type) ||
            !minic_type_equal(common_type, minic_type_long()) ||
            !minic_target_info_integer_literal_type(&target,
                                                    MINIC_INTEGER_LITERAL_BASE_DECIMAL,
                                                    false,
                                                    0U,
                                                    UINT64_C(32768),
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_long()) ||
            !minic_target_info_integer_literal_type(&target,
                                                    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL,
                                                    false,
                                                    0U,
                                                    UINT64_C(65535),
                                                    &literal_type) ||
            !minic_type_equal(literal_type, minic_type_unsigned_int())) {
            minic_c0_program_destroy(&program);
            return fail("synthetic target integer model is not data-driven");
        }
    }

    if (!minic_riscv64_type_layout(
            &program,
            byte_type,
            &byte_size,
            &byte_alignment) ||
        byte_size != 1U || byte_alignment != 1U) {
        minic_c0_program_destroy(&program);
        return fail("unsigned-char scalar layout");
    }

    if (!minic_riscv64_type_layout(
            &program,
            minic_type_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U ||
        !minic_riscv64_type_layout(
            &program,
            minic_type_unsigned_long(),
            &long_size,
            &long_alignment) ||
        long_size != 8U || long_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 long scalar layout");
    }

    if (!minic_type_is_float(minic_type_float()) ||
        minic_type_is_double(minic_type_float()) ||
        minic_type_is_integer(minic_type_float()) ||
        !minic_riscv64_type_layout(
            &program,
            minic_type_float(),
            &float_size,
            &float_alignment) ||
        float_size != 4U || float_alignment != 4U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 float scalar identity and layout");
    }

    if (!minic_riscv64_type_layout(
            &program,
            minic_type_double(),
            &double_size,
            &double_alignment) ||
        double_size != 8U || double_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 double scalar layout");
    }

    if (!minic_type_pointer_to(minic_type_void(), &void_pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("construct void pointer type");
    }
    alloc_parameter_types[0] = minic_type_unsigned_long();
    free_parameter_types[0] = void_pointer_type;
    if (!minic_c0_program_add_function_type(&program,
                                            void_pointer_type,
                                            alloc_parameter_types,
                                            1U,
                                            &alloc_function_type) ||
        !minic_c0_program_add_function_type(&program,
                                            minic_type_void(),
                                            free_parameter_types,
                                            1U,
                                            &free_function_type) ||
        !minic_type_pointer_to(alloc_function_type, &alloc_pointer_type) ||
        !minic_type_pointer_to(free_function_type, &free_pointer_type) ||
        !minic_riscv64_type_layout(&program,
                                   alloc_pointer_type,
                                   &function_pointer_size,
                                   &function_pointer_alignment) ||
        function_pointer_size != 8U || function_pointer_alignment != 8U) {
        minic_c0_program_destroy(&program);
        return fail("RV64 function pointer scalar layout");
    }
    if (!minic_c0_program_add_record(&program, "Hooks", 5U, &hooks_record_id) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "alloc", 5U, alloc_pointer_type, 1U) ||
        !minic_c0_record_add_field(
            &program, hooks_record_id, "free_fn", 7U, free_pointer_type, 1U) ||
        !minic_c0_program_finish_record(&program, hooks_record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct function pointer record");
    }

    if (!minic_c0_program_add_record(
            &program,
            "FloatPacket",
            11U,
            &floating_record_id) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "prefix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "value",
            5U,
            minic_type_double(),
            1U) ||
        !minic_c0_record_add_field(
            &program,
            floating_record_id,
            "suffix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_program_finish_record(&program, floating_record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct double record");
    }

    if (!minic_c0_program_add_record(
            &program,
            "Packet",
            6U,
            &record_id) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "prefix",
            6U,
            byte_type,
            1U) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "data",
            4U,
            minic_type_int(),
            4U) ||
        !minic_c0_record_add_field(
            &program,
            record_id,
            "value",
            5U,
            byte_type,
            1U) ||
        !minic_c0_program_finish_record(&program, record_id)) {
        minic_c0_program_destroy(&program);
        return fail("construct record");
    }
    record_type = minic_type_record(record_id);

    if (!minic_c0_program_add_block(&program, &block_id)) {
        minic_c0_program_destroy(&program);
        return fail("add block");
    }

    (void)memset(&local, 0, sizeof(local));
    local.type = byte_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add byte scalar");
    }

    local.element_count = 3U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add byte array");
    }

    local.type = minic_type_int();
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add int scalar");
    }

    local.element_count = 3U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add int array");
    }

    if (!minic_type_pointer_to(minic_type_int(), &pointer_type)) {
        minic_c0_program_destroy(&program);
        return fail("construct pointer type");
    }
    local.type = pointer_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add pointer scalar");
    }

    local.element_count = 2U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add pointer array");
    }

    local.type = minic_type_int();
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add trailing int");
    }

    local.type = record_type;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return fail("add record local");
    }

    if (!minic_c0_program_add_function(
            &program,
            "sample",
            6U,
            0U,
            8U,
            block_id,
            &function_id) ||
        !minic_riscv64_layout_program(
            "layout-test",
            &program,
            &diagnostic)) {
        minic_c0_program_destroy(&program);
        return fail("layout program");
    }

    hooks_record = minic_c0_program_record(&program, hooks_record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;

        if (hooks_record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(hooks_record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, hooks_record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, hooks_record, 1U, &offset1) ||
            size != 16U || alignment != 8U || offset0 != 0U || offset1 != 8U) {
            minic_c0_program_destroy(&program);
            return fail("function pointer record field layout");
        }
    }

    floating_record = minic_c0_program_record(&program, floating_record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;
        size_t offset2;

        if (floating_record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(floating_record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 1U, &offset1) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 2U, &offset2) ||
            size != 24U || alignment != 8U || offset0 != 0U || offset1 != 8U || offset2 != 16U) {
            minic_c0_program_destroy(&program);
            return fail("double record field layout");
        }
    }

    record = minic_c0_program_record(&program, record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;
        size_t offset2;

        if (record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 1U, &offset1) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 2U, &offset2) ||
            size != 24U || alignment != 4U || offset0 != 0U || offset1 != 4U || offset2 != 20U) {
            minic_c0_program_destroy(&program);
            return fail("record field layout");
        }
    }

    function = minic_c0_program_function(&program, function_id);
    {
        MinicRiscv64FunctionLayout function_layout;
        size_t expected_offsets[8] = {0U, 1U, 4U, 8U, 24U, 32U, 48U, 52U};
        size_t index;

        minic_riscv64_function_layout_initialize(&function_layout);
        if (function == NULL ||
            !minic_riscv64_layout_function(
                "layout-function", &program, function, &function_layout, &diagnostic) ||
            function_layout.local_count != 8U || function_layout.local_storage_size != 76U) {
            minic_riscv64_function_layout_destroy(&function_layout);
            minic_c0_program_destroy(&program);
            return fail("function side-state layout");
        }
        for (index = 0U; index < 8U; ++index) {
            size_t offset;

            if (!minic_riscv64_function_layout_local_offset(
                    &function_layout, function, index, &offset) ||
                offset != expected_offsets[index]) {
                minic_riscv64_function_layout_destroy(&function_layout);
                minic_c0_program_destroy(&program);
                return fail("function side-state local offsets");
            }
        }
        minic_riscv64_function_layout_destroy(&function_layout);
    }
    program.locals[1].element_count = 0U;
    diagnostic.message[0] = '\0';
    {
        MinicRiscv64FunctionLayout invalid_layout;

        minic_riscv64_function_layout_initialize(&invalid_layout);
        if (minic_riscv64_layout_function(
                "layout-zero", &program, function, &invalid_layout, &diagnostic) ||
            strcmp(diagnostic.message,
                   "local object size is invalid for the RV64 target") != 0) {
            minic_riscv64_function_layout_destroy(&invalid_layout);
            minic_c0_program_destroy(&program);
            return fail("zero-element byte object accepted");
        }
        minic_riscv64_function_layout_destroy(&invalid_layout);
    }

    program.locals[1].element_count = 3U;
    program.locals[3].element_count = SIZE_MAX;
    diagnostic.message[0] = '\0';
    {
        MinicRiscv64FunctionLayout invalid_layout;

        minic_riscv64_function_layout_initialize(&invalid_layout);
        if (minic_riscv64_layout_function(
                "layout-overflow", &program, function, &invalid_layout, &diagnostic) ||
            strcmp(diagnostic.message,
                   "local object size is invalid for the RV64 target") != 0) {
            minic_riscv64_function_layout_destroy(&invalid_layout);
            minic_c0_program_destroy(&program);
            return fail("array size overflow accepted");
        }
        minic_riscv64_function_layout_destroy(&invalid_layout);
    }

    {
        MinicGlobalObject object;
        MinicRecordId incomplete_record_id;
        MinicType incomplete_array_type;
        size_t size;
        size_t alignment;

        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_int();
        object.explicit_alignment = 16U;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 4U || alignment != 16U) {
            minic_c0_program_destroy(&program);
            return fail("explicit global object alignment query");
        }

        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_void();
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern void object layout query");
        }

        if (!minic_c0_program_add_record(&program, "Incomplete", 10U, &incomplete_record_id)) {
            minic_c0_program_destroy(&program);
            return fail("construct incomplete record type");
        }
        (void)memset(&object, 0, sizeof(object));
        object.type = minic_type_record(incomplete_record_id);
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete record layout query");
        }

        if (!minic_c0_program_add_incomplete_array_type(
                &program, minic_type_int(), &incomplete_array_type)) {
            minic_c0_program_destroy(&program);
            return fail("construct incomplete array type");
        }
        (void)memset(&object, 0, sizeof(object));
        object.type = incomplete_array_type;
        object.is_extern = true;
        if (!minic_data_layout_global_object(
                minic_default_data_layout(), &program, &object, &size, &alignment) ||
            size != 0U || alignment != 0U) {
            minic_c0_program_destroy(&program);
            return fail("extern incomplete array layout query");
        }
    }

    minic_c0_program_destroy(&program);
    (void)printf("PASS target/riscv64/layout\n");
    return 0;
}

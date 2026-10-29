/* M0 metadata-only ABI audit. No new target code is emitted here. */
#include "target/target_info.h"
#include "target/backend.h"
#include "target/riscv/instruction_select.h"
#include "frontend/parser.h"
#include "frontend/ast_verifier.h"

#include <stdio.h>
#include <string.h>

static int verify_lp64(const char *name,
                       MinicTargetArchitecture expected_arch,
                       MinicIntegerSign expected_char_sign,
                       MinicIntegerRank expected_wchar_rank,
                       MinicIntegerSign expected_wchar_sign) {
    const MinicTargetInfo *info = minic_target_info_from_name(name);
    const MinicDataLayout *layout;
    if (info == NULL || info->architecture != expected_arch ||
        info != minic_target_info_for_architecture(expected_arch) ||
        info->integer_model == NULL || info->data_layout == NULL) {
        (void)fprintf(stderr, "FAIL target identity: %s\n", name);
        return 1;
    }
    layout = info->data_layout;
    if (info->integer_model->plain_char_sign != expected_char_sign ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_BOOL] != 1U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_CHAR] != 8U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_SHORT] != 16U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_INT] != 32U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG] != 64U ||
        info->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_INT128] != 128U ||
        info->wide_character_type.integer_rank != expected_wchar_rank ||
        info->wide_character_type.integer_sign != expected_wchar_sign ||
        info->word_integer_rank != MINIC_INTEGER_RANK_LONG) {
        (void)fprintf(stderr, "FAIL integer/char/wchar ABI: %s\n", name);
        return 1;
    }
    if (!minic_data_layout_is_little_endian(layout) ||
        layout->pointer_size != 8U || layout->pointer_alignment != 8U ||
        layout->integer_size[MINIC_INTEGER_RANK_INT] != 4U ||
        layout->integer_size[MINIC_INTEGER_RANK_LONG] != 8U ||
        layout->integer_size[MINIC_INTEGER_RANK_INT128] != 16U ||
        layout->integer_alignment[MINIC_INTEGER_RANK_INT128] != 16U ||
        layout->float_size != 4U || layout->float_alignment != 4U ||
        layout->double_size != 8U || layout->double_alignment != 8U ||
        layout->long_double_size != 16U || layout->long_double_alignment != 16U) {
        (void)fprintf(stderr, "FAIL LP64 storage ABI: %s\n", name);
        return 1;
    }
    return 0;
}

/* RV32 is a distinct ILP32 consumer of the existing TargetInfo/DataLayout
 * model. Test GNU C language promotions, not only raw pointer bytes. */
static int verify_rv32(void) {
    const MinicTargetInfo *t = minic_target_info_from_name("riscv32-linux");
    MinicType v;
    if (t == NULL || t->architecture != MINIC_TARGET_ARCH_RISCV32 ||
        t->riscv_isa.xlen != 32U || t->riscv_isa.abi != MINIC_RISCV_ABI_ILP32 ||
        !minic_data_layout_is_little_endian(t->data_layout) ||
        t->data_layout->pointer_size != 4U ||
        t->data_layout->pointer_alignment != 4U ||
        t->data_layout->integer_size[MINIC_INTEGER_RANK_LONG] != 4U ||
        t->data_layout->integer_size[MINIC_INTEGER_RANK_LONG_LONG] != 8U ||
        t->data_layout->integer_alignment[MINIC_INTEGER_RANK_LONG_LONG] != 8U ||
        t->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG] != 32U ||
        t->integer_model->semantic_width_bits[MINIC_INTEGER_RANK_LONG_LONG] != 64U ||
        t->integer_model->plain_char_sign != MINIC_INTEGER_SIGN_UNSIGNED ||
        t->word_integer_rank != MINIC_INTEGER_RANK_INT ||
        t->variadic_scalar_read_kind != MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT ||
        t->variadic_linear_slot_bytes != 4U ||
        t->variadic_copy_kind != MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT ||
        !minic_target_info_integer_common(t, minic_type_unsigned_int(),
                                          minic_type_long(), &v) ||
        !minic_type_equal(v, minic_type_unsigned_long()) ||
        !minic_target_info_integer_common(t, minic_type_unsigned_long(),
                                          minic_type_long_long(), &v) ||
        !minic_type_equal(v, minic_type_long_long()))
        return (void)fprintf(stderr, "RV32_ILP32_MODEL=FAIL\n"), 1;
    return puts("RV32_ILP32_MODEL=PASS xlen=32 ptr=4 long=4 longlong=8 signedness=qualified") == EOF ? 1 : 0;
}

/* Parser/verifier language admission and ABI cursor identity must agree on
 * all five target descriptors. Declaration admission is not codegen support. */
static int verify_language_admission(void) {
    static const char *const names[] = {
        "riscv32-linux", "riscv64-linux", "x86_64-linux",
        "aarch64-linux", "loongarch64-linux"
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const MinicTargetInfo *t = minic_target_info_from_name(names[i]);
        MinicType vector_type;
        MinicType vector_pointer;
        bool is_arm = i == 3U;
        bool has_extended_float = i != 0U;
        bool has_vector;
        if (t == NULL ||
            minic_target_info_language_type_supported(
                t, MINIC_TYPE_BASE_FLOAT128) != has_extended_float ||
            minic_target_info_language_type_supported(
                t, MINIC_TYPE_BASE_FLOAT32X) != has_extended_float ||
            minic_target_info_language_type_supported(
                t, MINIC_TYPE_BASE_AARCH64_SVE_BOOL) != is_arm) {
            (void)fprintf(stderr, "FAIL target language/varargs contract: %s\\n", names[i]);
            return 1;
        }
        if (minic_target_info_header_attribute_supported(
                t, "__aarch64_vector_pcs__",
                sizeof("__aarch64_vector_pcs__") - 1U) != is_arm ||
            minic_target_info_header_attribute_supported(
                t, "__aarch64_vector_pcs__", 3U)) {
            (void)fprintf(stderr, "FAIL target header attribute contract: %s\\n", names[i]);
            return 1;
        }
        has_vector = minic_target_info_named_builtin_type(
            t, "__SVBool_t", sizeof("__SVBool_t") - 1U, &vector_type);
        if (!minic_type_pointer_to(
                minic_type_aarch64_header_builtin(
                    MINIC_TYPE_BASE_AARCH64_FLOAT32X4), &vector_pointer)) {
            (void)fprintf(stderr, "FAIL target vector pointer construction: %s\n", names[i]);
            return 1;
        }
        if (has_vector != is_arm ||
            (is_arm && vector_type.base_kind != MINIC_TYPE_BASE_AARCH64_SVE_BOOL) ||
            minic_target_info_header_opaque_vector_return(
                t, minic_type_aarch64_header_builtin(
                       MINIC_TYPE_BASE_AARCH64_FLOAT32X4)) != is_arm ||
            minic_target_info_header_opaque_vector_return(
                t, minic_type_float128()) ||
            minic_target_info_header_opaque_vector_return(
                t, vector_pointer) ||
            minic_target_info_named_builtin_type(
                t, "__SVBool_t", sizeof("__SVBool_t") - 2U, NULL) ||
            minic_target_info_named_builtin_type(
                t, "__not_a_builtin", sizeof("__not_a_builtin") - 1U, NULL)) {
            (void)fprintf(stderr, "FAIL target opaque builtin contract: %s\\n", names[i]);
            return 1;
        }
    }
    return puts("TARGET_LANGUAGE_ADMISSION=PASS five-targets") == EOF ? 1 : 0;
}

/* One target-owned C shape and one Program-owned type pair. Repeated
 * __builtin_va_list names and typedefs must share RecordId/ArrayTypeId;
 * source objects now use their true layout, not a pointer-sized proxy. */
static int verify_va_cursor_source_abi(void) {
    static const char *const names[] = {
        "riscv32-linux", "riscv64-linux", "x86_64-linux",
        "aarch64-linux", "loongarch64-linux"
    };
    static const char source[] =
        "typedef __builtin_va_list First;\n"
        "typedef __builtin_va_list Again;\n"
        "int forwarded(First a);\n"
        "int forwarded(Again a);\n"
        "struct VaBox { int key; First member; };\n";
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const MinicTargetInfo *t = minic_target_info_from_name(names[i]);
        MinicC0Program program;
        MinicVaCursorSourceShape shape;
        MinicC0AstVerifyFailure failure;
        MinicDiagnostic diagnostic = {0};
        MinicType source_type, state_type, pointer_state, void_pointer;
        size_t bytes, alignment;
        bool x86 = i == 2U;
        bool aarch = i == 3U;
        bool opaque = x86 || aarch;
        MinicTargetInfo missing_policy;
        minic_c0_program_initialize(&program);
        if (t == NULL ||
            !minic_target_info_va_cursor_source_shape(t, &shape) ||
            shape.kind != (x86 ? MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD_ARRAY_ONE :
                          aarch ? MINIC_VA_CURSOR_SOURCE_OPAQUE_RECORD :
                                  MINIC_VA_CURSOR_SOURCE_LINEAR_POINTER) ||
            shape.object_size != (x86 ? 24U : aarch ? 32U : t->data_layout->pointer_size) ||
            shape.object_alignment != t->data_layout->pointer_alignment ||
            !minic_parse_c0_program_for_target(names[i], source,
                                               sizeof(source) - 1U, t,
                                               &program, &diagnostic) ||
            !minic_c0_program_va_cursor_source_types(
                &program, &source_type, &state_type) ||
            !minic_type_pointer_to(minic_type_void(), &void_pointer) ||
            !minic_type_pointer_to(state_type, &pointer_state) ||
            !minic_data_layout_type(t->data_layout, &program, source_type,
                                    &bytes, &alignment) ||
            bytes != shape.object_size || alignment != shape.object_alignment ||
            program.record_count != (opaque ? 2U : 1U) ||
            program.array_type_count != (x86 ? 1U : 0U) ||
            program.type_alias_count != 2U ||
            !minic_type_equal(program.type_aliases[0].type, source_type) ||
            !minic_type_equal(program.type_aliases[1].type, source_type) ||
            !minic_target_info_va_cursor_source_operand_supported(
                t, &program, source_type, true) ||
            (opaque && minic_target_info_va_cursor_source_operand_supported(
                t, &program, void_pointer, true)) ||
            (x86 && !minic_target_info_va_cursor_source_operand_supported(
                t, &program, pointer_state, false)) ||
            !minic_c0_program_verify_target_detailed(
                &program, MINIC_C0_AST_PARSED, t, &failure)) {
            (void)fprintf(stderr,
                "FAIL target va cursor C source identity: %s diagnostic=%s records=%zu arrays=%zu\n",
                names[i], diagnostic.message, program.record_count, program.array_type_count);
            minic_c0_program_destroy(&program);
            return 1;
        }
        if (opaque) {
            /* ABI-state targets must never silently revert to an ordinary
             * void* source cursor when their source policy is missing. */
            missing_policy = *t;
            missing_policy.va_cursor_source_abi_policy = NULL;
            if (minic_target_info_va_cursor_source_shape(
                    &missing_policy, &shape)) {
                minic_c0_program_destroy(&program);
                (void)fprintf(stderr,
                    "FAIL target va cursor ABI-state missing source policy: %s\n",
                    names[i]);
                return 1;
            }
        }
        minic_c0_program_destroy(&program);
    }
    return puts("TARGET_VA_CURSOR_SOURCE_ABI=PASS x86_array24 aarch_record32 linear_isa3 canonical_tu5 typedef_identity") == EOF;
}

static int verify_registered_target_catalog(void) {
    const MinicTargetCatalogEntry *targets;
    const MinicDataLayout unknown_layout = {0};
    size_t i, count;
    unsigned int toolchain_count = 0U;
    targets = minic_target_catalog_all(&count);
    if (targets == NULL || count != 5U ||
        minic_data_layout_is_little_endian(NULL) ||
        minic_data_layout_is_little_endian(&unknown_layout) ||
        minic_target_catalog_find("not-a-target") != NULL ||
        minic_target_catalog_for_architecture((MinicTargetArchitecture)999) != NULL ||
        minic_target_catalog_default() !=
            minic_target_catalog_for_architecture(MINIC_TARGET_ARCH_RISCV64))
        return 1;
    for (i = 0U; i < count; ++i) {
        const MinicTargetCatalogEntry *entry =
            minic_target_catalog_find(targets[i].name);
        const MinicTargetInfo *info =
            minic_target_info_from_name(targets[i].name);
        const MinicBackend *backend = minic_backend_for_target(info);
        size_t j;
        if (entry != &targets[i] || info == NULL ||
            entry != minic_target_catalog_for_architecture(entry->architecture) ||
            info->architecture != entry->architecture ||
            backend == NULL || backend->emit_program == NULL ||
            info->data_layout == NULL ||
            (entry->isa_family == MINIC_TARGET_ISA_FAMILY_RISCV) !=
                (info->riscv_isa.xlen != 0U) ||
            !(entry->product_driver_stages &
              MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C))
            return 1;
        for (j = 0U; j < i; ++j) {
            const MinicTargetInfo *other =
                minic_target_info_from_name(targets[j].name);
            if (other == info || minic_backend_for_target(other) == backend)
                return 1;
        }
        if (entry->product_driver_stages &
            MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER) {
            if (entry->architecture != MINIC_TARGET_ARCH_RISCV64)
                return 1;
            ++toolchain_count;
        }
    }
    if (toolchain_count != 1U) return 1;
    {
        MinicTargetInfo unknown = *minic_default_target_info();
        unknown.architecture = (MinicTargetArchitecture)999;
        if (minic_backend_for_target(&unknown) != NULL) return 1;
    }
    return puts("TARGET_CATALOG=PASS five_backends=5 full_staged=rv64_only single_registration=1") == EOF;
}

static int verify_riscv_profile_catalog(void) {
    const MinicRiscvProfile *profiles;
    size_t n, i, j;
    profiles = minic_riscv_profile_all(&n);
    if (profiles == NULL || n != 6U ||
        minic_riscv_profile_find("rv64gc") != NULL ||
        minic_riscv_profile_find("rv32im") != NULL ||
        minic_riscv_profile_find(NULL) != NULL)
        return 1;
    for (i = 0U; i < n; ++i) {
        const MinicRiscvProfile *p = &profiles[i];
        if (p != minic_riscv_profile_find(p->march) ||
            (p->xlen != 32U && p->xlen != 64U) ||
            ((p->xlen == 32U) != (p->kind == MINIC_RISCV_PROFILE_RV32I)) ||
            strcmp(p->mabi, p->xlen == 32U ? "ilp32" :
                   p->kind == MINIC_RISCV_PROFILE_RV64IMAFD ? "lp64d" : "lp64") != 0 ||
            (p->codegen_features & ~p->features) != 0U ||
            (p->xlen == 32U && p->features != 0U))
            return 1;
        for (j = i + 1U; j < n; ++j)
            if (strcmp(p->march, profiles[j].march) == 0)
                return 1;
    }
    if (minic_riscv_profile_find("rv64ima_zicsr_zifencei") == NULL ||
        minic_riscv_profile_find("rv64imac_zicsr_zifencei") == NULL)
        return 1;
    return puts("RISCV_PROFILE_CATALOG=PASS six_explicit_profiles Base_I_source_asm_supersets") == EOF;
}

static int verify_invocation_language_abi(void) {
    const MinicTargetCatalogEntry *entries;
    size_t i, count;
    entries = minic_target_catalog_all(&count);
    for (i = 0U; i < count; ++i) {
        const MinicTargetInfo *canonical =
            minic_target_info_from_name(entries[i].name);
        MinicTargetInvocation invocation, second;
        MinicIntegerSign original;
        MinicType original_wchar;
        if (canonical == NULL ||
            !minic_target_info_plain_char_sign(canonical, &original) ||
            !minic_target_info_wide_character_type(canonical, &original_wchar) ||
            !minic_target_invocation_begin(&invocation, canonical) ||
            !minic_target_invocation_begin(&second, canonical) ||
            !minic_target_invocation_plain_char(
                &invocation, original == MINIC_INTEGER_SIGN_SIGNED
                    ? MINIC_INTEGER_SIGN_UNSIGNED : MINIC_INTEGER_SIGN_SIGNED) ||
            !minic_target_invocation_short_wchar(&invocation, true) ||
            invocation.target.integer_model != &invocation.integer_model ||
            invocation.integer_model.plain_char_sign == original ||
            second.integer_model.plain_char_sign != original ||
            canonical->integer_model->plain_char_sign != original ||
            !minic_type_equal(second.target.wide_character_type, original_wchar) ||
            !minic_target_invocation_short_wchar(&invocation, false) ||
            !minic_type_equal(invocation.target.wide_character_type, original_wchar))
            return (void)fprintf(stderr,
                "TARGET_INVOCATION_ABI=FAIL target:%s\n", entries[i].name), 1;
    }
    return puts("TARGET_INVOCATION_ABI=PASS five_targets immutable_models signed_char short_wchar_restore") == EOF;
}

static int verify_riscv_integer_strategy(void) {
    /* Only the machine selector sees optional M. The same typed Core
     * operation remains compilable through a Base-I libcall on both XLENs.
     * C/other extensions are irrelevant until a qualified emitter consumes
     * them. The synthetic RV32M subtarget is a selector test only: the
     * CLI does not yet claim RV32M machine/ABI certification. */
    const MinicCoreInstructionKind ops[] = {
        MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,
        MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE,
        MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER
    };
    const char *expected_native[][2] = {
        {"mul", "mul"}, {"div", "divu"}, {"rem", "remu"}
    };
    size_t x, op, sign;
    for (x = 0U; x < 2U; ++x) {
        MinicRiscvIsa base = {
            .xlen = x == 0U ? 32U : 64U,
            .abi = x == 0U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64,
            .features = 0U,
            .strict_isa = true
        };
        MinicRiscvIsa extended = base;
        MinicRiscvIntegerPlan plan;
        extended.features = MINIC_RISCV_FEATURE_M | MINIC_RISCV_FEATURE_C;
        extended.codegen_features = MINIC_RISCV_FEATURE_M;
        for (op = 0U; op < sizeof(ops) / sizeof(ops[0]); ++op) {
            for (sign = 0U; sign < 2U; ++sign) {
                const char *sym = minic_riscv_integer_arithmetic_symbol(
                    ops[op], sign != 0U, base.xlen);
                if (sym == NULL ||
                    !minic_riscv_plan_integer_arithmetic(
                        &base, ops[op], sign != 0U, base.xlen, &plan) ||
                    plan.strategy != MINIC_RISCV_INTEGER_RUNTIME_LIBCALL ||
                    plan.opcode != NULL ||
                    plan.symbol == NULL ||
                    strcmp(plan.symbol, sym) != 0)
                    return 1;
                if (!minic_riscv_plan_integer_arithmetic(
                        &extended, ops[op], sign != 0U, base.xlen, &plan) ||
                    plan.strategy != MINIC_RISCV_INTEGER_NATIVE_M ||
                    plan.symbol != NULL ||
                    strcmp(plan.opcode, expected_native[op][sign]) != 0)
                    return 1;
                /* Target still supports M, but this invocation emits I. */
                extended.codegen_features = 0U;
                if (!minic_riscv_plan_integer_arithmetic(
                        &extended, ops[op], sign != 0U, base.xlen, &plan) ||
                    plan.strategy != MINIC_RISCV_INTEGER_RUNTIME_LIBCALL)
                    return 1;
                extended.features = MINIC_RISCV_FEATURE_M | MINIC_RISCV_FEATURE_C;
        extended.codegen_features = MINIC_RISCV_FEATURE_M;
            }
        }
        if (minic_riscv_plan_integer_arithmetic(
                &base, ops[0], false, base.xlen == 32U ? 64U : 32U, &plan) ||
            minic_riscv_plan_integer_arithmetic(
                &base, MINIC_CORE_INSTRUCTION_INTEGER_ADD, false,
                base.xlen, &plan))
            return 1;
    }
    return puts("RISCV_BASE_I_STRATEGY=PASS rv32_rv64 M_optional C_ignored runtime_fallback") == EOF;
}

/* One typed Core CALL_FRAME_ADDRESS invariant under separate machine ABIs.
 * AArch64 owns a genuine x29 frame record (so caller-level 1 works);
 * LoongArch owns current fixed SP, while SysV x86 owns RBP.
 * Levels above the implemented target contract remain prohibited. */
static int verify_lp64_return_address_level_zero(void) {
    const MinicTargetInfo *aarch =
        minic_target_info_from_name("aarch64-linux");
    const MinicTargetInfo *x86 =
        minic_target_info_from_name("x86_64-linux");
    const MinicTargetInfo *loong =
        minic_target_info_from_name("loongarch64-linux");
    if (!aarch || !x86 || !loong ||
        !minic_target_info_call_frame_address_supported(
            aarch, MINIC_CALL_FRAME_ADDRESS_RETURN, 0U) ||
        minic_target_info_call_frame_address_supported(
            aarch, MINIC_CALL_FRAME_ADDRESS_RETURN, 1U) ||
        !minic_target_info_call_frame_address_supported(
            aarch, MINIC_CALL_FRAME_ADDRESS_FRAME, 0U) ||
        !minic_target_info_call_frame_address_supported(
            aarch, MINIC_CALL_FRAME_ADDRESS_FRAME, 1U) ||
        minic_target_info_call_frame_address_supported(
            aarch, MINIC_CALL_FRAME_ADDRESS_FRAME, 2U) ||
        !minic_target_info_call_frame_address_supported(
            x86, MINIC_CALL_FRAME_ADDRESS_RETURN, 0U) ||
        !minic_target_info_call_frame_address_supported(
            x86, MINIC_CALL_FRAME_ADDRESS_FRAME, 0U) ||
        minic_target_info_call_frame_address_supported(
            x86, MINIC_CALL_FRAME_ADDRESS_RETURN, 1U) ||
        minic_target_info_call_frame_address_supported(
            x86, MINIC_CALL_FRAME_ADDRESS_FRAME, 1U) ||
        !minic_target_info_call_frame_address_supported(
            loong, MINIC_CALL_FRAME_ADDRESS_RETURN, 0U) ||
        minic_target_info_call_frame_address_supported(
            loong, MINIC_CALL_FRAME_ADDRESS_RETURN, 1U) ||
        !minic_target_info_call_frame_address_supported(
            loong, MINIC_CALL_FRAME_ADDRESS_FRAME, 0U) ||
        minic_target_info_call_frame_address_supported(
            loong, MINIC_CALL_FRAME_ADDRESS_FRAME, 1U)) {
        (void)fprintf(stderr, "LP64_CALL_FRAME=FAIL\n");
        return 1;
    }
    (void)puts("LP64_CALL_FRAME=PASS aarch64_x30_sp loongarch64_ra_sp x86_saved_pc_and_rbp level0_only");
    return 0;
}

static int verify_fixed_register_target_contract(void) {
    static const char *names[] = {
        "riscv32-linux", "riscv64-linux", "x86_64-linux",
        "aarch64-linux", "loongarch64-linux"
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const MinicTargetInfo *t = minic_target_info_from_name(names[i]);
        bool riscv = i <= 1U;
        bool x86 = i == 2U;
        bool arm64 = i == 3U;
        if (!t ||
            minic_target_info_fixed_register_supported(t, "rsp", 3U) != x86 ||
            minic_target_info_fixed_register_supported(t, "sp", 2U) != (riscv || arm64) ||
            minic_target_info_fixed_register_supported(t, "tp", 2U) != riscv ||
            minic_target_info_fixed_register_supported(t, "r15", 3U) ||
            minic_target_info_fixed_register_supported(t, "rbp", 3U) ||
            minic_target_info_fixed_register_supported(t, "rsp", 2U)) {
            (void)fprintf(stderr, "FIXED_REGISTER_TARGET=FAIL %s\n", names[i]);
            return 1;
        }
    }
    {
        const MinicTargetInfo *rv64 = minic_target_info_from_name("riscv64-linux");
        const MinicTargetInfo *rv32 = minic_target_info_from_name("riscv32-linux");
        if (rv64 == NULL || rv32 == NULL ||
            !minic_target_info_local_fixed_register_supported(rv64, "a7", 2U) ||
            !minic_target_info_local_fixed_register_supported(rv64, "x17", 3U) ||
            minic_target_info_local_fixed_register_supported(rv64, "x7", 2U) ||
            !minic_target_info_inline_asm_register_clobber_supported(rv64, "t2", 2U) ||
            !minic_target_info_inline_asm_register_clobber_supported(rv64, "x7", 2U) ||
            !minic_target_info_inline_asm_register_clobber_supported(rv64, "x31", 3U) ||
            minic_target_info_inline_asm_register_clobber_supported(rv64, "x8", 2U) ||
            minic_target_info_inline_asm_register_clobber_supported(rv64, "sp", 2U) ||
            minic_target_info_local_fixed_register_supported(rv32, "x17", 3U) ||
            minic_target_info_inline_asm_register_clobber_supported(rv32, "x7", 2U)) {
            (void)fprintf(stderr, "RISCV_PHYSICAL_REGISTER_ADMISSION=FAIL\n");
            return 1;
        }
        (void)puts("RISCV_PHYSICAL_REGISTER_ADMISSION=PASS rv64_aliases rv32_fail_closed");
    }
    (void)puts("FIXED_REGISTER_TARGET=PASS rv_sp_tp x86_rsp_live aarch64_sp_live other_GPRs_fail_closed");
    return 0;
}

/* An identical GNU constraint letter is not an identical ISA predicate:
 * RV/LoongArch: signed imm12; AArch64: unsigned imm12 with optional LSL12;
 * x86: shift count 0..31. Parser admission NEVER qualifies machine ASM. */
static int verify_gnu_asm_immediate_target_contract(void) {
    static const char *const names[] = {
        "riscv32-linux", "riscv64-linux", "x86_64-linux",
        "aarch64-linux", "loongarch64-linux"
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const MinicTargetInfo *target = minic_target_info_from_name(names[i]);
        const bool signed12 = (i == 0U || i == 1U || i == 4U);
        const bool arm = i == 3U;
        const bool x86 = i == 2U;
        if (target == NULL ||
            minic_target_info_inline_asm_immediate_may_defer(target) != (arm || x86 || i == 4U) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, -2048) != signed12 ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 0) != (signed12 || arm || x86) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 0x401) != (signed12 || arm) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 2047) != (signed12 || arm) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 31) != (signed12 || arm || x86) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 32) != (signed12 || arm) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, -1) != signed12 ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, -2049) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 2048) != arm ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 4095) != arm ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 4096) != arm ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 0xfff000) != arm ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 0x100001) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "I", 1U, 0x1000000) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "K", 1U, 18) ||
            minic_target_info_inline_asm_immediate_constraint_supported(target, "II", 2U, 18) ||
            minic_target_info_inline_asm_register_clobber_supported(target, "t0", 2U) !=
                (i == 0U || i == 1U || i == 4U)) {
            (void)fprintf(stderr, "GNU_ASM_IMMEDIATE_TARGET=FAIL target=%s\n", names[i]);
            return 1;
        }
    }
    (void)puts("GNU_ASM_IMMEDIATE_TARGET=PASS signed12_rv_loong arm_unsigned12_opt_shift x86_shift_0_31 templates_fail_closed");
    return 0;
}

/* Every target source admission policy is independent of the physical ASM
 * machine emitter. The old single target_info ISA switch used to conflate
 * GNU parser spelling, hard-register object identity and emitted clobbers. */
static int verify_target_asm_source_policy(void) {
    static const char *const names[] = {
        "riscv32-linux", "riscv64-linux", "x86_64-linux",
        "aarch64-linux", "loongarch64-linux"
    };
    const MinicTargetInfo *rv32 = minic_target_info_from_name(names[0]);
    const MinicTargetInfo *rv64 = minic_target_info_from_name(names[1]);
    const MinicTargetInfo *x86 = minic_target_info_from_name(names[2]);
    const MinicTargetInfo *arm = minic_target_info_from_name(names[3]);
    const MinicTargetInfo *loong = minic_target_info_from_name(names[4]);
    MinicTargetInfo unsupported;
    MinicType result, arguments[3];
    size_t argument_count, immediate_index, i;
    const char *mnemonic;
    char callee[128];
    unsigned int stage = 0U;
    if (!rv32 || !rv64 || !x86 || !arm || !loong) return 1;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        stage = 1U;
        const MinicTargetInfo *target = minic_target_info_from_name(names[i]);
        if (!target->asm_source_policy ||
            !minic_target_info_inline_asm_memory_input_constraint_supported(
                target, "m", 1U) ||
            minic_target_info_inline_asm_memory_input_constraint_supported(
                target, "Q", 1U) != (target == arm)) goto failure;
    }
    unsupported = *x86;
    stage = 2U;
    unsupported.asm_source_policy = NULL;
    if (minic_target_info_fixed_register_supported(&unsupported, "rsp", 3U) ||
        minic_target_info_inline_asm_memory_input_constraint_supported(
            &unsupported, "m", 1U) ||
        minic_target_info_inline_asm_source_clobber_supported(
            &unsupported, "cc", 2U)) goto failure;
    stage = 3U;
    if (!minic_target_info_local_fixed_register_supported(x86, "%rax", 4U) ||
        minic_target_info_local_fixed_register_supported(x86, "%r15", 4U) ||
        minic_target_info_inline_asm_register_clobber_supported(x86, "rbx", 3U) ||
        !minic_target_info_inline_asm_source_clobber_supported(x86, "ebx", 3U) ||
        !minic_target_info_inline_asm_source_clobber_supported(x86, "flags", 5U) ||
        !minic_target_info_inline_asm_readwrite_hardreg_supported(
            x86, "rsp", 3U, "+r", 2U, minic_type_unsigned_long()) ||
        minic_target_info_inline_asm_readwrite_hardreg_supported(
            x86, "rsp", 3U, "+r", 2U, minic_type_int())) goto failure;
    stage = 4U;
    if (!minic_target_info_local_fixed_register_supported(arm, "x30", 3U) ||
        !minic_target_info_local_fixed_register_supported(arm, "r17", 3U) ||
        minic_target_info_local_fixed_register_supported(arm, "x18", 3U) ||
        !minic_target_info_inline_asm_register_clobber_supported(arm, "cc", 2U) ||
        minic_target_info_fixed_register_write_supported(arm, "sp", 2U))
        goto failure;
    stage = 5U;
    if (!minic_target_info_fixed_register_supported(loong, "$r21", 4U) ||
        !minic_target_info_fixed_register_write_supported(loong, "$tp", 3U) ||
        minic_target_info_fixed_register_write_supported(loong, "$sp", 3U) ||
        !minic_target_info_local_fixed_register_supported(loong, "a7", 2U) ||
        minic_target_info_local_fixed_register_supported(loong, "t1", 2U) ||
        !minic_target_info_inline_asm_source_clobber_supported(loong, "$s0", 3U))
        goto failure;
    stage = 6U;
    if (!minic_target_info_local_fixed_register_supported(rv32, "a7", 2U) ||
        minic_target_info_local_fixed_register_supported(rv32, "x17", 3U) ||
        !minic_target_info_inline_asm_register_clobber_supported(rv32, "t6", 2U) ||
        !minic_target_info_local_fixed_register_supported(rv64, "x17", 3U) ||
        minic_target_info_local_fixed_register_supported(rv64, "x7", 2U))
        goto failure;
    stage = 7U;
    if (!minic_target_info_machine_builtin_signature(loong,
            "__builtin_loongarch_crc_w_b_w", sizeof("__builtin_loongarch_crc_w_b_w") - 1U,
            callee, sizeof(callee), &result, arguments, &argument_count,
            &mnemonic, &immediate_index) ||
        !minic_type_equal(result, minic_type_int()) ||
        !minic_type_is_plain_char(arguments[0]) ||
        !minic_type_is_signed_integer(arguments[0]) ||
        argument_count != 2U || immediate_index != SIZE_MAX ||
        strcmp(mnemonic, "crc.w.b.w") != 0 ||
        minic_target_info_machine_builtin_signature(x86,
            "__builtin_loongarch_crc_w_b_w", sizeof("__builtin_loongarch_crc_w_b_w") - 1U,
            callee, sizeof(callee), &result, arguments, &argument_count,
            &mnemonic, &immediate_index)) goto failure;
    (void)puts("TARGET_ASM_SOURCE_POLICY=PASS five_independent_targets grammar_vs_machine_fail_closed loong_builtin_owned");
    return 0;
failure:
    (void)fprintf(stderr, "TARGET_ASM_SOURCE_POLICY=FAIL stage=%u\n", stage);
    return 1;
}

int main(void) {
    const MinicTargetInfo *rv = minic_default_target_info();
    const MinicTargetInfo *x86 = minic_target_info_from_name("x86_64-linux");
    const MinicTargetInfo *arm = minic_target_info_from_name("aarch64-linux");
    const MinicTargetInfo *loong = minic_target_info_from_name("loongarch64-linux");
    if (rv == NULL || x86 == NULL || arm == NULL || loong == NULL ||
        rv == arm || x86 == arm || rv == loong || x86 == loong ||
        arm == loong || rv->architecture != MINIC_TARGET_ARCH_RISCV64 ||
        x86->architecture != MINIC_TARGET_ARCH_X86_64 ||
        minic_target_info_from_name("mips64-linux") != NULL ||
        minic_target_info_from_name(NULL) != NULL ||
        minic_target_info_for_architecture((MinicTargetArchitecture)999) != NULL) {
        (void)fprintf(stderr, "FAIL four independent target identities\n");
        return 1;
    }
    /* Both AAPCS64 (32-byte tag) and SysV x86 (24-byte tag) now allocate
     * independent mutable cursor state; RV64/Loong remain scalar cursors. */
    if (arm->variadic_scalar_read_kind != MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR ||
        arm->variadic_copy_kind != MINIC_VARIADIC_COPY_ABI_STATE ||
        x86->variadic_copy_kind != MINIC_VARIADIC_COPY_ABI_STATE ||
        rv->variadic_copy_kind != MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT ||
        loong->variadic_copy_kind != MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT) {
        (void)fprintf(stderr, "FAIL ABI-state va_copy must own separate mutable tag on AAPCS64 and x86\n");
        return 1;
    }
    if (verify_lp64_return_address_level_zero() != 0 ||
        verify_fixed_register_target_contract() != 0 ||
        verify_gnu_asm_immediate_target_contract() != 0 ||
        verify_target_asm_source_policy() != 0 ||
        verify_riscv_integer_strategy() != 0 || verify_registered_target_catalog() != 0 || verify_riscv_profile_catalog() != 0 ||
        verify_lp64("riscv64-linux", MINIC_TARGET_ARCH_RISCV64,
                    MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_SIGNED) != 0 ||
        verify_invocation_language_abi() != 0 || verify_language_admission() != 0 ||
        verify_va_cursor_source_abi() != 0 || verify_rv32() != 0 ||
        verify_lp64("aarch64-linux", MINIC_TARGET_ARCH_AARCH64,
                    MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_UNSIGNED) != 0 ||
        verify_lp64("loongarch64-linux", MINIC_TARGET_ARCH_LOONGARCH64,
                    MINIC_INTEGER_SIGN_SIGNED, MINIC_INTEGER_RANK_INT,
                    MINIC_INTEGER_SIGN_SIGNED) != 0) return 1;
    (void)puts("FOUR_BACKEND_TARGET_MODELS_M0=PASS registered=4 distinct_layouts=4 aarch64_char=unsigned loongarch64_char=signed no_machine_codegen_claim=1");
    return 0;
}

#include "target/x86_64/machine_internal.h"
#include "target/x86_64/codegen_options.h"
#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/asm_contract.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* In early head code a direct-global SSA pointer normally stays physical,
 * including when passed to an out-of-line data helper. If a pointer has
 * instead passed through a numeric link-VMA cast and is converted back to a
 * pointer, only its LOAD/STORE memory use is rebased onto physical memory.
 * Subtracting the link base preserves field and dynamic pointer offsets. */
static bool x86_head_load_memory_address(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    MinicCoreValueId address) {
    MinicCoreGlobalId global;
    const char *symbol;
    if (!x86_value_load(out, frame, address, "%r10")) return false;
    if (!frame->kernel_head_link_addresses ||
        frame->kernel_head_memory_rebase_globals == NULL)
        return true;
    if (address >= fn->value_count) return false;
    global = frame->kernel_head_memory_rebase_globals[address];
    if (global == MINIC_CORE_GLOBAL_INVALID) return true;
    if (global >= fn->global_count || fn->globals[global].name == NULL)
        return false;
    symbol = fn->globals[global].name;
    return fprintf(out,
        "  movq $%s, %%r11\n"
        "  subq %%r11, %%r10\n"
        "  leaq %s(%%rip), %%r11\n"
        "  addq %%r11, %%r10\n", symbol, symbol) >= 0;
}

/* Qualified GNU `i` symbolic operands are resolved by Common Core. Every
 * machine consumer shares this fail-closed relocation validator: a symbol
 * with an optional signed decimal addend is representable by ELF, whereas
 * a register, expression, assembler directive or string interpolation is
 * not. This validation runs before anything is written to the output. */
static bool x86_asm_symbol_relocation_valid(const char *text) {
    size_t i, length;
    if (!text) return false;
    length = strlen(text);
    if (length == 0U || length > 128U ||
        !((text[0] >= 'a' && text[0] <= 'z') ||
          (text[0] >= 'A' && text[0] <= 'Z') ||
          text[0] == '_' || text[0] == '.')) return false;
    for (i = 1U; i < length; ++i) {
        const char ch = text[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '$')
            continue;
        if ((ch == '+' || ch == '-') && i + 1U < length) {
            size_t j;
            for (j = i + 1U; j < length; ++j)
                if (text[j] < '0' || text[j] > '9') return false;
            break;
        }
        return false;
    }
    return true;
}

/* Linux's bundled ZSTD decoder places four progressively narrower GNU
 * basic-asm alignments around an explicitly padded hot loop.  A basic asm
 * `nop` is part of that same source sequence.  Match the entire text and
 * reject every other opaque instruction, including appended directives:
 * opaque asm must never become a generic pass-through to GNU as. */
static unsigned int x86_linux_zstd_loop_padding_form(
    const MinicCoreInlineAsm *a) {
    static const char *const forms[] = {
        "nop", ".p2align 3", ".p2align 4", ".p2align 5"
    };
    size_t i;
    if (a == NULL || a->template_text == NULL || !a->is_volatile ||
        a->is_goto || a->has_memory_clobber ||
        a->register_clobber_count != 0U ||
        a->resolved_immediate_count != 0U)
        return 0U;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t length = strlen(forms[i]);
        if (a->template_length == length &&
            memcmp(a->template_text, forms[i], length) == 0)
            return (unsigned int)(i + 1U);
    }
    return 0U;
}

/* Linux static-key patching needs three genuine *link-time* relocations.
 * On x86 the patch-site and target use PREL32, and the key/branch-flags
 * entry uses PREL64. Core's symbolic specialization can resolve the key
 * even when source asm sees a formal parameter of __always_inline__; the
 * machine emitter must not guess from a runtime load or a raw GNU string. */
static bool emit_linux_jump_label_asm_goto(
    FILE *out, const MinicCoreFunction *fn, const MinicCoreInlineAsm *a,
    size_t function_id) {
    unsigned long long flags;
    char *end;
    if (!out || !fn || !a || !a->is_goto || !a->is_volatile ||
        a->has_memory_clobber || a->register_clobber_count != 0U ||
        a->resolved_immediate_count != 2U ||
        a->goto_target >= fn->block_count ||
        a->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_linux_jump_label_form(
            a->template_text, a->template_length) ||
        !a->resolved_immediates[1] ||
        !x86_asm_symbol_relocation_valid(a->resolved_immediates[0]))
        return false;
    flags = strtoull(a->resolved_immediates[1], &end, 10);
    if (end == a->resolved_immediates[1] || *end != '\0' || flags > 3ULL)
        return false;
    return fprintf(out,
        "1: jmp .Lmini_x86_f%zu_b%u\n"
        "  .pushsection __jump_table,\"aw\"\n"
        "  .balign 8\n"
        "  .long 1b - .\n"
        "  .long .Lmini_x86_f%zu_b%u - .\n"
        "  .quad %s + %llu - .\n"
        "  .popsection\n",
        function_id, (unsigned)a->goto_target,
        function_id, (unsigned)a->goto_target,
        a->resolved_immediates[0], flags) >= 0;
}

/* Linux _static_cpu_has: the two real C destinations are represented in
 * Core, and Core has already resolved three `i` expressions into owned
 * source constants. Target x86 is responsible for physical branching,
 * PC-relative boot_cpu_data relocation and both ELF patch-table records.
 * A second C label or an unresolved symbol must NEVER fall into raw asm. */
static bool emit_linux_cpu_has_asm_goto(
    FILE *out, const MinicCoreFunction *fn, const MinicCoreInlineAsm *a,
    size_t function_id) {
    const char *text, *end;
    unsigned long long feature, mask;
    char *parsed;
    if (!out || !fn || !a || !a->is_goto || !a->is_volatile ||
        a->has_memory_clobber || a->register_clobber_count != 0U ||
        a->resolved_immediate_count != 3U ||
        a->goto_target >= fn->block_count ||
        a->goto_second_target >= fn->block_count ||
        a->goto_target == a->goto_second_target ||
        !minic_x86_64_linux_cpu_has_goto_form(
            a->template_text, a->template_length) ||
        !a->resolved_immediates[0] || !a->resolved_immediates[1] ||
        !a->resolved_immediates[2]) return false;
    feature = strtoull(a->resolved_immediates[0], &parsed, 10);
    if (parsed == a->resolved_immediates[0] || *parsed || feature > 65535U)
        return false;
    mask = strtoull(a->resolved_immediates[1], &parsed, 10);
    if (parsed == a->resolved_immediates[1] || *parsed ||
        mask != (1ULL << (feature & 7ULL))) return false;
    /* Relocations must identify a *single* compiler-resolved symbol with an
     * optional decimal addend. No assembler expression, dereference, '@',
     * semicolon, parentheses or alternate section can enter through %P. */
    if (!x86_asm_symbol_relocation_valid(a->resolved_immediates[2]))
        return false;
    text = strstr(a->template_text, ".skip ");
    end = text ? strchr(text, '\n') : NULL;
    if (!text || !end || end <= text ||
        fputs("661:\n  jmp 6f\n662:\n", out) == EOF ||
        fwrite(text, 1U, (size_t)(end - text + 1), out) !=
            (size_t)(end - text + 1)) return false;
    return fprintf(out,
        "663:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n"
        "  .long 6641f - .\n"
        "  .4byte (3*32+21)\n"
        "  .byte 663b-661b\n"
        "  .byte 6651f-6641f\n"
        "  .long 661b - .\n"
        "  .long 6642f - .\n"
        "  .4byte %llu\n"
        "  .byte 663b-661b\n"
        "  .byte 6652f-6642f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n"
        "  jmp .Lmini_x86_f%zu_b%u\n"
        "6651:\n"
        "6642:\n"
        "6652:\n"
        "  .popsection\n"
        "  .pushsection .altinstr_aux,\"ax\"\n"
        "6:\n"
        "  testb $%llu, %s(%%rip)\n"
        "  jnz .Lmini_x86_f%zu_b%u\n"
        "  jmp .Lmini_x86_f%zu_b%u\n"
        "  .popsection\n",
        feature, function_id, (unsigned)a->goto_second_target,
        mask, a->resolved_immediates[2],
        function_id, (unsigned)a->goto_target,
        function_id, (unsigned)a->goto_second_target) >= 0;
}

/* Native x86-64 machine stage, cleanly separated from function/TU owner.
 * Native x87, SysV, integer and memory emitters retain their tested
 * register and instruction sequences. */
/* GNU overflow builtins calculate in mathematical (unbounded) precision,
 * then store the truncated value and report whether it fits the *result*
 * type. Linux overflows_type()/check_mul_overflow() legitimately mix u64,
 * signed long, u32 and narrower pointer targets. The existing matched-width
 * fast path cannot handle these; evaluate the complete 128-bit signed value
 * with real x86 GP arithmetic instead of narrowing inputs prematurely. */
static bool x86_emit_mixed_integer_overflow(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins) {
    const MinicCoreValueId lhs = ins->value.integer_overflow.left;
    const MinicCoreValueId rhs = ins->value.integer_overflow.right;
    const MinicCoreValueId address = ins->value.integer_overflow.result_address;
    MinicType result_type;
    size_t lhs_width, rhs_width, result_width;
    bool lhs_signed, rhs_signed, result_signed;
    const char *lhs_extension, *rhs_extension, *result_extension, *store;
    if (!out || !program || !target || !fn || !frame ||
        ins->result >= fn->value_count || lhs >= fn->value_count ||
        rhs >= fn->value_count || address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result_type) ||
        !minic_type_is_integer(result_type) ||
        minic_type_is_bool_integer(result_type) ||
        minic_type_is_const(result_type) ||
        minic_type_is_volatile(result_type) ||
        !minic_x86_64_scalar_width(program, target, fn->values[lhs].type,
                                    &lhs_width, &lhs_signed) ||
        !minic_x86_64_scalar_width(program, target, fn->values[rhs].type,
                                    &rhs_width, &rhs_signed) ||
        !minic_x86_64_scalar_width(program, target, result_type,
                                    &result_width, &result_signed))
        return false;
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        break;
    default: return false;
    }
    lhs_extension = lhs_width == 1U ? (lhs_signed ? "movsbq %al, %rax" : "movzbl %al, %eax") :
                    lhs_width == 2U ? (lhs_signed ? "movswq %ax, %rax" : "movzwl %ax, %eax") :
                    lhs_width == 4U ? (lhs_signed ? "movslq %eax, %rax" : "movl %eax, %eax") : NULL;
    rhs_extension = rhs_width == 1U ? (rhs_signed ? "movsbq %r10b, %r10" : "movzbl %r10b, %r10d") :
                    rhs_width == 2U ? (rhs_signed ? "movswq %r10w, %r10" : "movzwl %r10w, %r10d") :
                    rhs_width == 4U ? (rhs_signed ? "movslq %r10d, %r10" : "movl %r10d, %r10d") : NULL;
    result_extension = result_width == 1U ?
        (result_signed ? "movsbq %al, %r11" : "movzbl %al, %r11d") :
        result_width == 2U ?
        (result_signed ? "movswq %ax, %r11" : "movzwl %ax, %r11d") :
        result_width == 4U ?
        (result_signed ? "movslq %eax, %r11" : "movl %eax, %r11d") :
        "movq %rax, %r11";
    store = result_width == 1U ? "movb %al, (%r10)" :
            result_width == 2U ? "movw %ax, (%r10)" :
            result_width == 4U ? "movl %eax, (%r10)" : "movq %rax, (%r10)";
    if (!x86_value_load(out, frame, lhs, "%rax") ||
        !x86_value_load(out, frame, rhs, "%r10") ||
        (lhs_extension && fprintf(out, "  %s\n", lhs_extension) < 0) ||
        (rhs_extension && fprintf(out, "  %s\n", rhs_extension) < 0))
        return false;
    if (ins->value.integer_overflow.operator_kind ==
        MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
        /* Unsigned MUL produces all 128 product bits. The two sign-limb
         * corrections convert that to exact two's-complement mixed signed
         * multiplication (no 64-bit signed-overflow UB in the emitter). */
        if (fputs("  movq %rax, %r11\n"
                  "  movq %rax, %r8\n", out) == EOF ||
            fputs(lhs_signed ? "  sarq $63, %r8\n" : "  xorq %r8, %r8\n", out) == EOF ||
            fputs("  andq %r10, %r8\n"
                  "  movq %r10, %r9\n", out) == EOF ||
            fputs(rhs_signed ? "  sarq $63, %r9\n" : "  xorq %r9, %r9\n", out) == EOF ||
            fputs("  andq %r11, %r9\n"
                  "  mulq %r10\n"
                  "  subq %r8, %rdx\n"
                  "  subq %r9, %rdx\n"
                  "  movq %rdx, %r8\n", out) == EOF)
            return false;
    } else {
        if (fputs("  movq %rax, %r8\n", out) == EOF ||
            fputs(lhs_signed ? "  sarq $63, %r8\n" : "  xorq %r8, %r8\n", out) == EOF ||
            fputs("  movq %r10, %r9\n", out) == EOF ||
            fputs(rhs_signed ? "  sarq $63, %r9\n" : "  xorq %r9, %r9\n", out) == EOF ||
            fputs(ins->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD
                    ? "  addq %r10, %rax\n  adcq %r9, %r8\n"
                    : "  subq %r10, %rax\n  sbbq %r9, %r8\n", out) == EOF)
            return false;
    }
    if (!x86_value_load(out, frame, address, "%r10") ||
        fprintf(out, "  %s\n  %s\n", store, result_extension) < 0 ||
        fputs("  cmpq %r11, %rax\n"
              "  setne %r9b\n"
              "  movq %r11, %r10\n", out) == EOF ||
        fputs(result_signed ? "  sarq $63, %r10\n" : "  xorq %r10, %r10\n", out) == EOF ||
        fputs("  cmpq %r10, %r8\n"
              "  setne %r10b\n"
              "  orb %r10b, %r9b\n"
              "  movzbq %r9b, %rax\n", out) == EOF)
        return false;
    return x86_value_store(out, frame, ins->result);
}

bool minic_x86_64_emit_core_instruction(FILE *out, const MinicC0Program *program,
                                 const MinicTargetInfo *target, const MinicCoreFunction *fn,
                                 const MinicX86Frame *frame, const MinicCoreInstruction *ins,
                                 size_t function_id, size_t instruction_id) {
    MinicCoreValueId left, right;
    size_t bytes;
    bool sign;
    if (ins == NULL) return false;
    /* Machine-independent family categorization, native emission per ISA. */
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_FLOATING_SCALAR)
        return minic_x86_64_emit_fp_instruction(out, program, target, fn,
                                                frame, ins);
    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
        return minic_x86_64_emit_structured_asm(
            out, program, target, fn, frame, ins, function_id);
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        const size_t asm_id = ins->value.inline_asm_id;
        const MinicCoreInlineAsm *a = fn != NULL &&
            asm_id < fn->inline_asm_count ? &fn->inline_asms[asm_id] : NULL;
        static const char *const privileged[] = {
            "  cli\n", "  sti\n", "  sti\n  hlt\n", "  hlt\n",
            "  wbinvd\n", "  swapgs\n", "  .byte 0x0f, 0x01, 0xe8\n",
            "  outb %al, $0x80\n", "  outb %al, $0xed\n",
            "  int3\n"
        };
        unsigned int form;
        if (a != NULL && a->is_goto) {
            if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL &&
                !minic_x86_64_linux_cpu_has_goto_form(
                    a->template_text, a->template_length) &&
                !minic_x86_64_linux_jump_label_form(
                    a->template_text, a->template_length))
                (void)fprintf(stderr,
                    "X86_TARGET_ASM_GOTO_FRONTIER function=%s length=%zu "
                    "second=%u immediate_count=%zu preview=%.90s\n",
                    fn->name, a->template_length, (unsigned)a->goto_second_target,
                    a->resolved_immediate_count, a->template_text);
            if (minic_x86_64_linux_jump_label_form(
                    a->template_text, a->template_length))
                return emit_linux_jump_label_asm_goto(out, fn, a, function_id);
            return emit_linux_cpu_has_asm_goto(out, fn, a, function_id);
        }
        form = a != NULL ? minic_x86_64_linux_privileged_zero_form(
            a->template_text, a->template_length) : 0U;
        if (form != 0U) {
            /* Each source form has no register or C operand and carries an
             * explicit "memory" clobber. No arbitrary opaque assembler
             * payload may reach this privileged instruction switch. */
            if (!a->is_volatile ||
                (form != 4U && form != 8U && form != 9U &&
                 form != 10U && !a->has_memory_clobber) ||
                a->register_clobber_count != 0U || form > 10U)
                return false;
            return fputs(privileged[form - 1U], out) != EOF;
        }
        if (a != NULL && minic_x86_64_linux_zstd_p2align_form(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || a->is_goto || a->has_memory_clobber ||
                a->register_clobber_count != 0U) return false;
            return fputs("  .p2align 6\n", out) != EOF;
        }
        if (a != NULL) {
            const unsigned int padding = x86_linux_zstd_loop_padding_form(a);
            if (padding != 0U) {
                static const char *const emitted[] = {
                    "  nop\n", "  .p2align 3\n", "  .p2align 4\n",
                    "  .p2align 5\n"
                };
                return fputs(emitted[padding - 1U], out) != EOF;
            }
        }
        if (a != NULL && minic_x86_64_linux_amx_tilerelease_form(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || a->is_goto || a->has_memory_clobber ||
                a->register_clobber_count != 0U) return false;
            return fputs("  .byte 0xc4, 0xe2, 0x78, 0x49, 0xc0\n", out) != EOF;
        }
        if (a != NULL && minic_x86_64_linux_x87_fwait_fault_form(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || a->is_goto || a->has_memory_clobber ||
                a->register_clobber_count != 0U) return false;
            return fputs("1: fwait\n2:\n"
                         "  .pushsection \"__ex_table\",\"a\"\n"
                         "  .balign 4\n"
                         "  .long (1b) - .\n"
                         "  .long (2b) - .\n"
                         "  .long 1\n"
                         "  .popsection\n", out) != EOF;
        }
        if (a != NULL && minic_x86_64_linux_weak_wrmsr_fence_form(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || a->is_goto || !a->has_memory_clobber ||
                a->register_clobber_count != 0U) return false;
            return fputs(
                "661:\n  mfence\n  lfence\n"
                "662:\n"
                "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                "((6651f-6641f)-(662b-661b)),0x90\n"
                "663:\n  .pushsection .altinstructions,\"a\"\n"
                "  .long 661b - .\n"
                "  .long 6641f - .\n"
                "  .4byte (((1 << 0) << 16) | ((11*32+27)))\n"
                "  .byte 663b-661b\n"
                "  .byte 6651f-6641f\n"
                "  .popsection\n"
                "  .pushsection .altinstr_replacement,\"ax\"\n"
                "6641:\n6651:\n  .popsection\n", out) != EOF;
        }
        if (a != NULL && minic_x86_64_linux_smap_alternative_form(
                a->template_text, a->template_length))
            return minic_x86_64_emit_linux_smap_alternative(
                out, fn, frame, NULL, a);
        if (a != NULL && minic_x86_64_linux_lfence_alternative_form(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || !a->has_memory_clobber ||
                a->register_clobber_count || a->is_goto)
                return false;
            return fputs(
                "661:\n"
                "662:\n"
                "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                    "((6651f-6641f)-(662b-661b)),0x90\n"
                "663:\n"
                "  .pushsection .altinstructions,\"a\"\n"
                "  .long 661b - .\n"
                "  .long 6641f - .\n"
                "  .4byte (20*32+2)\n"
                "  .byte 663b-661b\n"
                "  .byte 6651f-6641f\n"
                "  .popsection\n"
                "  .pushsection .altinstr_replacement,\"ax\"\n"
                "6641: lfence\n"
                "6651:\n"
                "  .popsection\n", out) != EOF;
        }
        if (a != NULL && minic_x86_64_linux_unreachable_opaque(
                a->template_text, a->template_length)) {
            if (!a->is_volatile || a->is_goto || a->has_memory_clobber ||
                a->register_clobber_count) return false;
            return fwrite(a->template_text, 1U, a->template_length, out) ==
                   a->template_length && fputc('\n', out) != EOF;
        }
        if (a == NULL || a->is_goto || a->register_clobber_count != 0U ||
            !minic_x86_64_linux_bug_table_opaque(
                a->template_text, a->template_length)) {
            if (getenv("MINIC_ASM_FRONTIER_TRACE") != NULL &&
                a != NULL && a->template_text != NULL) {
                char preview[81];
                size_t i, limit = a->template_length;
                if (limit > sizeof(preview) - 1U) limit = sizeof(preview) - 1U;
                for (i = 0U; i < limit; ++i) {
                    const char c = a->template_text[i];
                    preview[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
                }
                preview[limit] = '\0';
                (void)fprintf(stderr,
                    "X86_TARGET_ASM_FRONTIER function=%s length=%zu "
                    "goto=%d volatile=%d mem=%d preview=%s\n",
                    fn->name, a->template_length, a->is_goto,
                    a->is_volatile, a->has_memory_clobber, preview);
            }
            return false;
        }
        return fputs("  ", out) != EOF &&
               fwrite(a->template_text, 1U, a->template_length, out) ==
                   a->template_length &&
               (a->template_length == 0U ||
                a->template_text[a->template_length - 1U] == '\n' ||
                fputc('\n', out) != EOF);
    }
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_CALL)
        return minic_x86_64_emit_call_instruction(out, program, target, fn,
                                                  frame, ins, instruction_id);
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        const MinicCoreCallFrameAddressKind kind =
            ins->value.call_frame_address.kind;
        const MinicCallFrameAddressKind source_kind =
            kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                ? MINIC_CALL_FRAME_ADDRESS_RETURN
                : MINIC_CALL_FRAME_ADDRESS_FRAME;
        /* SysV prologue retains caller-PC at 8(rbp) and an invariant RBP
         * frame anchor. No fabricated code pointer or chain unwinding. */
        if (!minic_multi_direct_call_frame_level0_shape(fn, ins, kind) ||
            !minic_target_info_call_frame_address_supported(
                target, source_kind, ins->value.call_frame_address.level) ||
            fprintf(out, kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                             ? "  movq 8(%%rbp), %%rax\n"
                             : "  movq %%rbp, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (minic_type_is_int128_integer(ins->type)) {
            if (fprintf(out, "  movabsq $%" PRId64 ", %%rax\n",
                        ins->value.integer_value) < 0 ||
                fprintf(out, minic_type_is_signed_integer(ins->type)
                                    ? "  movq %%rax, %%rdx\n  sarq $63, %%rdx\n"
                                    : "  xorq %%rdx, %%rdx\n") < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!minic_x86_64_scalar_width(program, target, ins->type, &bytes, &sign) ||
            fprintf(out, "  movabsq $%" PRId64 ", %%rax\n", ins->value.integer_value) < 0)
            return false;
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_PARAMETER: {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            frame->incoming_arguments.calling_convention);
        const bool ms_x64 = frame->incoming_arguments.calling_convention ==
            MINIC_CALLING_CONVENTION_MS_X64;
        size_t position = ins->value.parameter_index;
        const MinicX86ArgLocation *location;
        if (abi == NULL || position >= frame->incoming_arguments.argument_count ||
            frame->incoming_arguments.locations == NULL)
            return false;
        location = &frame->incoming_arguments.locations[position];
        if (minic_type_is_int128_integer(ins->type)) {
            if (ms_x64 || location->kind != MINIC_X86_ARG_I128_TWO_GP)
                return false;
            if (!location->on_stack) {
                if (location->gp_index >= abi->gp_argument_count - 1U ||
                    (frame->saves_incoming_gp
                     ? fprintf(out,
                         "  movq -%zu(%%rbp), %%r10\n"
                         "  movq -%zu(%%rbp), %%r11\n",
                         frame->incoming_gp_save_offset +
                             location->gp_index * 8U,
                         frame->incoming_gp_save_offset +
                             (location->gp_index + 1U) * 8U)
                     : fprintf(out,
                         "  movq %s, %%r10\n"
                         "  movq %s, %%r11\n",
                         abi->gp_arguments[location->gp_index],
                         abi->gp_arguments[location->gp_index + 1U])) < 0)
                    return false;
            } else if (fprintf(out,
                    "  movq %zu(%%rbp), %%r10\n"
                    "  movq %zu(%%rbp), %%r11\n",
                    16U + location->stack_index * 8U,
                    24U + location->stack_index * 8U) < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%r10", "%r11");
        }
        if (minic_type_is_long_double(ins->type)) {
            if (ms_x64 || location->kind != MINIC_X86_ARG_X87_MEMORY ||
                fprintf(out, "  fldt %zu(%%rbp)\n",
                        16U + location->stack_index * 8U) < 0)
                return false;
            return x86_x87_store(out, frame, ins->result);
        }
        if (minic_type_is_double(ins->type) || minic_type_is_float(ins->type)) {
            if (location->kind != MINIC_X86_ARG_SSE) return false;
            if (!location->on_stack) {
                if (location->fp_index >= abi->fp_argument_count ||
                    fprintf(out, minic_type_is_double(ins->type)
                             ? "  movq %%xmm%zu, %%rax\n"
                             : "  movd %%xmm%zu, %%eax\n",
                            location->fp_index) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
        } else {
            if (location->kind != MINIC_X86_ARG_INTEGER) return false;
            if (!location->on_stack) {
                if (location->gp_index >= abi->gp_argument_count ||
                    (frame->saves_incoming_gp
                     ? fprintf(out, "  movq -%zu(%%rbp), %%rax\n",
                         frame->incoming_gp_save_offset +
                             location->gp_index * 8U)
                     : fprintf(out, "  movq %s, %%rax\n",
                         abi->gp_arguments[location->gp_index])) < 0)
                    return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
            if (!x86_normalize(out, program, target, ins->type)) return false;
        }
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;
        /* One Core binding/type/SSA validator is shared with AArch64.
         * x86 owns only its ABI-specific live RSP move and frame store.
         * Unknown fixed registers remain rejected in TargetInfo. */
        if (!minic_multi_direct_fixed_register_read_shape(
                program, target, fn, ins, &binding) ||
            binding->register_name_length != 3U ||
            memcmp(binding->register_name, "rsp", 3U) != 0)
            return false;
        return fprintf(out, "  movq %%rsp, %%rax\n") >= 0 &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
            MINIC_CALLING_CONVENTION_DEFAULT);
        const size_t position = ins->value.parameter_object.parameter_index;
        const MinicCoreObjectId object = ins->value.parameter_object.object_id;
        const MinicX86ArgLocation *location;
        const bool type_compatible = position < fn->parameter_count &&
            object < fn->object_count &&
            minic_x86_64_record_abi_compatible(
                fn->objects[object].type, fn->parameter_types[position]);
        if (abi == NULL || frame->incoming_arguments.calling_convention !=
                MINIC_CALLING_CONVENTION_DEFAULT ||
            position >= frame->incoming_arguments.argument_count ||
            frame->incoming_arguments.locations == NULL ||
            object >= fn->object_count || !type_compatible)
            return false;
        location = &frame->incoming_arguments.locations[position];
        if (location->kind == MINIC_X86_ARG_RECORD_EMPTY)
            return frame->object_storage.objects != NULL &&
                   frame->object_storage.objects[object].extent == 0U;
        if (location->kind != MINIC_X86_ARG_RECORD_ONE_GP &&
            location->kind != MINIC_X86_ARG_RECORD_TWO_GP &&
            location->kind != MINIC_X86_ARG_RECORD_MEMORY)
            return false;
        if (!x86_object_address(out, frame, object, "%r10"))
            return false;
        if (location->kind == MINIC_X86_ARG_RECORD_MEMORY) {
            size_t j, source;
            if (!location->on_stack || location->stack_index > SIZE_MAX / 8U ||
                location->stack_index * 8U > (size_t)INT32_MAX - 16U ||
                location->record_bytes > (size_t)INT32_MAX - 16U -
                                          location->stack_index * 8U)
                return false;
            source = 16U + location->stack_index * 8U;
            for (j = 0U; j + 8U <= location->record_bytes; j += 8U)
                if (fprintf(out,
                    "  movq %zu(%%rbp), %%rax\n"
                    "  movq %%rax, %zu(%%r10)\n",
                    source + j, j) < 0) return false;
            for (; j < location->record_bytes; ++j)
                if (fprintf(out,
                    "  movzbq %zu(%%rbp), %%rax\n"
                    "  movb %%al, %zu(%%r10)\n",
                    source + j, j) < 0) return false;
            return true;
        }
        if (location->kind == MINIC_X86_ARG_RECORD_ONE_GP) {
            if (!location->on_stack) {
                if (location->gp_index >= abi->gp_argument_count) return false;
                if (fprintf(out, "  movq %s, %%rax\n",
                            abi->gp_arguments[location->gp_index]) < 0) return false;
            } else if (fprintf(out, "  movq %zu(%%rbp), %%rax\n",
                               16U + location->stack_index * 8U) < 0)
                return false;
            return x86_one_gp_store(out, location->record_bytes);
        }
        if (!location->on_stack) {
            if (location->gp_index >= abi->gp_argument_count - 1U) return false;
            return fprintf(out,
                "  movq %s, 0(%%r10)\n"
                "  movq %s, 8(%%r10)\n",
                abi->gp_arguments[location->gp_index],
                abi->gp_arguments[location->gp_index + 1U]) >= 0;
        }
        return fprintf(out,
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 0(%%r10)\n"
            "  movq %zu(%%rbp), %%rax\n"
            "  movq %%rax, 8(%%r10)\n",
            16U + location->stack_index * 8U,
            24U + location->stack_index * 8U) >= 0;
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (ins->value.global_id >= fn->global_count ||
            fn->globals[ins->value.global_id].name == NULL) return false;
        /* A direct data global keeps its RIP-relative physical pointer for
         * direct LOAD/STORE and CALL argument users. A pointer-to-integer
         * consumer later turns its own result into the link kernel VMA,
         * without changing this (possibly shared) SSA pointer producer. */
        if (frame->kernel_head_link_addresses &&
            !(frame->kernel_head_direct_data_globals != NULL &&
              frame->kernel_head_direct_data_globals[ins->value.global_id]))
            return fprintf(out, "  movq $%s, %%rax\n",
                           fn->globals[ins->value.global_id].name) >= 0 &&
                   x86_value_store(out, frame, ins->result);
        /* In non-PIC code (including original -fno-PIE kernel C), even a
         * default-visible external uses a direct RIP-relative relocation:
         * the static link resolves it without allocating any GOT entry.
         * In PIC, only hidden/internal/TU-local objects are nonpreemptible;
         * default-visible externals still require the GOT in a DSO. */
        if (!target->position_independent ||
            minic_target_info_elf_object_is_nonpreemptible(
                program, fn->globals[ins->value.global_id].name))
            return fprintf(out, "  leaq %s(%%rip), %%rax\n",
                           fn->globals[ins->value.global_id].name) >= 0 &&
                   x86_value_store(out, frame, ins->result);
        /* A PIC default-visible symbol may be interposed by a shared
         * library. Preserve its GOTPCREL relocation; the static kernel
         * -fno-PIE path above does not invent an interposition requirement. */
        return fprintf(out, "  movq %s@GOTPCREL(%%rip), %%rax\n",
                       fn->globals[ins->value.global_id].name) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (ins->value.function_symbol_id >= fn->function_symbol_count ||
            fn->function_symbols[ins->value.function_symbol_id].name == NULL) return false;
        if (!target->position_independent ||
            minic_target_info_elf_function_is_nonpreemptible(
                program, fn->function_symbols[ins->value.function_symbol_id].name))
            return fprintf(out, "  leaq %s(%%rip), %%rax\n",
                           fn->function_symbols[ins->value.function_symbol_id].name) >= 0 &&
                   x86_value_store(out, frame, ins->result);
        /* Only PIC preemptible function addresses require an actual GOT
         * indirection; non-PIC static executables resolve direct PC32. */
        return fprintf(out, "  movq %s@GOTPCREL(%%rip), %%rax\n",
                       fn->function_symbols[ins->value.function_symbol_id].name) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {
        MinicCoreBlockId destination;
        /* The address of a GNU &&label is a function-local code pointer.
         * Keep it RIP-relative and preserve the Core block identity: the
         * shared CFG filter keeps all source label blocks as roots. */
        return minic_multi_direct_block_address_shape(fn, ins, &destination) &&
               fprintf(out, "  leaq .Lmini_x86_f%zu_b%u(%%rip), %%rax\n",
                       function_id, (unsigned int)destination) >= 0 &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
        /* SysV __va_list_tag is {u32 gp_offset,u32 fp_offset,
         * void *overflow_arg_area,void *reg_save_area}.
         * GP entries consume 8 bytes (0..48), SSE entries 16 bytes
         * (48..176), and overflow arguments consume eight stack bytes.
         * Only scalar integer, pointer and double are supported here. */
        bool is_fp = minic_type_is_double(ins->type);
        size_t ignored_width;
        bool ignored_signed;
        if (ins->result >= fn->value_count ||
            ins->value.operand >= fn->value_count ||
            !minic_type_is_pointer(fn->values[ins->value.operand].type) ||
            (!is_fp && !minic_x86_64_scalar_width(program, target, ins->type,
                                         &ignored_width, &ignored_signed)))
            return false;
        if (!x86_value_load(out, frame, ins->value.operand, "%r10"))
            return false;
        if (fprintf(out, "  movl %u(%%r10), %%ecx\n"
                         "  cmpl $%u, %%ecx\n"
                         "  jae .Lmini_x86_va_%zu_%u_stack\n"
                         "  movq 16(%%r10), %%r11\n"
                         "  movq (%%r11,%%rcx), %%rax\n"
                         "  addl $%u, %u(%%r10)\n"
                         "  jmp .Lmini_x86_va_%zu_%u_done\n"
                         ".Lmini_x86_va_%zu_%u_stack:\n"
                         "  movq 8(%%r10), %%r11\n"
                         "  movq (%%r11), %%rax\n"
                         "  addq $8, %%r11\n"
                         "  movq %%r11, 8(%%r10)\n"
                         ".Lmini_x86_va_%zu_%u_done:\n",
                    is_fp ? 4U : 0U, is_fp ? 176U : 48U,
                    function_id, (unsigned)ins->result,
                    is_fp ? 16U : 8U, is_fp ? 4U : 0U,
                    function_id, (unsigned)ins->result,
                    function_id, (unsigned)ins->result,
                    function_id, (unsigned)ins->result) < 0)
            return false;
        if (!is_fp && !x86_normalize(out, program, target, ins->type))
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY: {
        /* 24-byte SysV __va_list_tag = two u32 offsets plus two pointers.
         * Copy three aligned eightbytes into the private tag allocated by
         * VARIADIC_ARGUMENT_ADDRESS. Do not alias the source cursor. */
        if (!x86_value_load(out, frame,
                            ins->value.variadic_copy.destination, "%r10") ||
            !x86_value_load(out, frame,
                            ins->value.variadic_copy.source, "%r11"))
            return false;
        return fprintf(out,
                "  movq 0(%%r11), %%rax\n"
                "  movq %%rax, 0(%%r10)\n"
                "  movq 8(%%r11), %%rax\n"
                "  movq %%rax, 8(%%r10)\n"
                "  movq 16(%%r11), %%rax\n"
                "  movq %%rax, 16(%%r10)\n") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t i;
        size_t tag_offset = 0U;
        if (frame->varargs_count == 0U || ins->result >= fn->value_count ||
            !minic_type_is_pointer(ins->type)) return false;
        for (i = 0U; i < frame->varargs_count; ++i) {
            if (frame->varargs_results[i] == ins->result) {
                tag_offset = frame->varargs_tag_base_offset + (i + 1U) * 24U;
                break;
            }
        }
        if (i == frame->varargs_count) return false;
        /* %rax is a pointer to the 24-byte x86 SysV __va_list_tag.
         * Native libc's vsnprintf reads and updates exactly this layout. */
        if (fprintf(out,
                    "  leaq -%zu(%%rbp), %%rax\n"
                    "  movl $%u, 0(%%rax)\n"
                    "  movl $%u, 4(%%rax)\n"
                    "  leaq %zu(%%rbp), %%r10\n"
                    "  movq %%r10, 8(%%rax)\n"
                    "  leaq -%zu(%%rbp), %%r10\n"
                    "  movq %%r10, 16(%%rax)\n",
                    tag_offset, frame->varargs_gp_offset, frame->varargs_fp_offset,
                    16U + frame->varargs_fixed_stack_slots * 8U,
                    frame->varargs_regsave_offset) < 0) return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (ins->value.object_id >= frame->object_count) return false;
        /* This per-instruction flag is set only after Core proves that
         * the next LOAD/STORE reads this exact object's pointer once. The
         * next instruction's identity is tracked separately for its load. */
        if (frame->direct_object_address_producer)
            return true;
        if (frame->objects_from_aligned_rsp) {
            if (fprintf(out, "  leaq %zu(%%rsp), %%rax\n",
                        frame->object_offsets[ins->value.object_id]) < 0) return false;
        } else if (fprintf(out, "  leaq -%zu(%%rbp), %%rax\n",
                           frame->object_offsets[ins->value.object_id]) < 0) {
            return false;
        }
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        /* The source is an address-backed record *value*, not a scalar
         * register result. Materialize an independent owned Core object
         * using exact byte-wise reads, including volatile source bytes.
         * This mirrors the common DirectLP64 record-load contract. */
        MinicType source_pointee, source_plain, target_plain, object_plain;
        size_t extent, alignment, offset;
        const MinicCoreObjectId dest =
            ins->value.record_load.destination_object;
        const MinicCoreValueId source =
            ins->value.record_load.source_address;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_record(ins->type) ||
            dest >= frame->object_count || dest >= fn->object_count ||
            source >= fn->value_count ||
            !minic_type_pointee(fn->values[source].type, &source_pointee) ||
            !minic_type_unqualified(source_pointee, &source_plain) ||
            !minic_type_unqualified(ins->type, &target_plain) ||
            !minic_type_unqualified(fn->objects[dest].type, &object_plain) ||
            !minic_type_equal(source_plain, target_plain) ||
            !minic_type_equal(object_plain, target_plain) ||
            !minic_data_layout_type(target->data_layout, program,
                                    target_plain, &extent, &alignment) ||
            !alignment ||
            extent > frame->object_storage.objects[dest].extent ||
            !x86_value_load(out, frame, source, "%r11"))
            return false;
        if (frame->objects_from_aligned_rsp) {
            if (fprintf(out, "  leaq %zu(%%rsp), %%r10\n",
                        frame->object_offsets[dest]) < 0) return false;
        } else if (fprintf(out, "  leaq -%zu(%%rbp), %%r10\n",
                           frame->object_offsets[dest]) < 0) return false;
        for (offset = 0U; offset < extent; ++offset)
            if (fprintf(out,
                    "  movzbq %zu(%%r11), %%rax\n"
                    "  movb %%al, %zu(%%r10)\n",
                    offset, offset) < 0)
                return false;
        return true;
    }
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        /* Byte-preserving copy between already-evaluated addresses.
         * Do not assume field layout or alignment; match RV64 semantics. */
        MinicType dst_pointee, src_pointee, dst_unqualified, src_unqualified;
        size_t record_size, record_alignment, copied;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_record(ins->type) ||
            ins->value.record_copy.destination_address >= fn->value_count ||
            ins->value.record_copy.source_address >= fn->value_count ||
            !minic_type_pointee(fn->values[ins->value.record_copy.destination_address].type,
                                &dst_pointee) ||
            !minic_type_pointee(fn->values[ins->value.record_copy.source_address].type,
                                &src_pointee) ||
            !minic_type_unqualified(dst_pointee, &dst_unqualified) ||
            !minic_type_unqualified(src_pointee, &src_unqualified) ||
            !minic_type_equal(dst_unqualified, ins->type) ||
            !minic_type_equal(src_unqualified, ins->type) ||
            !minic_data_layout_type(target->data_layout, program, ins->type,
                                    &record_size, &record_alignment) ||
            record_alignment == 0U ||
            !x86_value_load(out, frame, ins->value.record_copy.destination_address, "%r10") ||
            !x86_value_load(out, frame, ins->value.record_copy.source_address, "%r11"))
            return false;
        /* GNU zero-size records have no bytes to copy. */
        copied = 0U;
        while (copied < record_size) {
            size_t chunk = record_size - copied;
            size_t offset;
            if (chunk > 2048U) chunk = 2048U;
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(out,
                            "  movzbq %zu(%%r11), %%rax\n"
                            "  movb %%al, %zu(%%r10)\n",
                            offset, offset) < 0)
                    return false;
            }
            copied += chunk;
            if (copied < record_size &&
                fprintf(out, "  addq $%zu, %%r10\n  addq $%zu, %%r11\n",
                        chunk, chunk) < 0)
                return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId source = ins->value.operand;
        MinicType source_type;
        if (source >= fn->value_count) return false;
        source_type = fn->values[source].type;
        if (minic_type_is_int128_integer(ins->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!x86_i128_load(out, frame, source, "%rax", "%rdx"))
                    return false;
            } else {
                size_t source_bytes;
                bool signed_source;
                if (!minic_x86_64_scalar_width(program, target, source_type,
                                      &source_bytes, &signed_source) ||
                    !x86_value_load(out, frame, source, "%rax") ||
                    fprintf(out, signed_source
                                ? "  movq %%rax, %%rdx\n  sarq $63, %%rdx\n"
                                : "  xorq %%rdx, %%rdx\n") < 0)
                    return false;
            }
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (minic_type_is_int128_integer(source_type))
            return x86_i128_load(out, frame, source, "%rax", "%rdx") &&
                   x86_normalize(out, program, target, ins->type) &&
                   x86_value_store(out, frame, ins->result);
        return x86_value_load(out, frame, source, "%rax") &&
               x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
        const MinicCoreValueId operand = ins->value.operand;
        MinicCoreGlobalId vma_global = MINIC_CORE_GLOBAL_INVALID;
        size_t src_bytes;
        bool src_sign;
        if (operand >= fn->value_count ||
            !minic_core_scalar_bitcast_types_valid(ins->type, fn->values[operand].type) ||
            !minic_x86_64_scalar_width(program, target, fn->values[operand].type,
                              &src_bytes, &src_sign) ||
            !minic_x86_64_scalar_width(program, target, ins->type, &bytes, &sign)) return false;
        /* Core bitcast models pointer/integer conversions, not only same-width
         * reinterpretation. Source values are already canonical in 64-bit
         * Core slots, and the destination normalization applies truncation. */
        (void)src_bytes;
        (void)src_sign;
        if (frame->kernel_head_link_addresses &&
            frame->kernel_head_vma_cast_globals != NULL &&
            ins->result < fn->value_count)
            vma_global = frame->kernel_head_vma_cast_globals[ins->result];
        if (vma_global != MINIC_CORE_GLOBAL_INVALID) {
            const char *symbol;
            if (vma_global >= fn->global_count ||
                fn->globals[vma_global].name == NULL) return false;
            symbol = fn->globals[vma_global].name;
            /* Convert this use's physical pointer to a numeric link VMA;
             * field/dynamic offsets survive because the adjustment is by
             * the difference between the two bases of the SAME symbol. */
            return x86_value_load(out, frame, operand, "%rax") &&
                   fprintf(out,
                       "  leaq %s(%%rip), %%r11\n"
                       "  subq %%r11, %%rax\n"
                       "  movq $%s, %%r11\n"
                       "  addq %%r11, %%rax\n", symbol, symbol) >= 0 &&
                   x86_normalize(out, program, target, ins->type) &&
                   x86_value_store(out, frame, ins->result);
        }
        return x86_value_load(out, frame, operand, "%rax") &&
               x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_long_double(ins->type))
            return x86_head_load_memory_address(out, fn, frame, ins->value.load.address) &&
                   fprintf(out, "  fldt (%%r10)\n") >= 0 &&
                   x86_x87_store(out, frame, ins->result);
        if (minic_type_is_int128_integer(ins->type))
            return x86_head_load_memory_address(out, fn, frame, ins->value.load.address) &&
                   fprintf(out, "  movq (%%r10), %%rax\n"
                                "  movq 8(%%r10), %%rdx\n") >= 0 &&
                   x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        if (!minic_x86_64_storage_width(program, target, ins->type, &bytes, &sign)) return false;
        if (frame->direct_object_address_consumer &&
            frame->direct_object_address_value_id == ins->value.load.address) {
            char memory[64];
            return x86_object_memory_operand(frame,
                       frame->direct_object_address_object_id,
                       memory, sizeof(memory)) &&
                   x86_load_memory_at(out, bytes, sign, memory) &&
                   x86_value_store(out, frame, ins->result);
        }
        return x86_head_load_memory_address(out, fn, frame, ins->value.load.address) &&
               x86_load_memory(out, bytes, sign) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
        const MinicCoreValueId addr = ins->value.atomic_load.address;
        MinicType pointee;
        if (ins->value.atomic_load.memory_order != 0U ||
            addr >= fn->value_count ||
            !minic_type_pointee(fn->values[addr].type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
            !minic_x86_64_storage_width(program, target, pointee, &bytes, &sign))
            return false;
        /* Aligned scalar x86 MOV has atomicity at supported widths. */
        return x86_head_load_memory_address(out, fn, frame, addr) &&
               x86_load_memory(out, bytes, sign) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE: {
        const MinicCoreValueId src = ins->value.atomic_store.stored_value;
        const MinicCoreValueId addr = ins->value.atomic_store.address;
        MinicType pointee;
        if (ins->value.atomic_store.memory_order != 0U ||
            src >= fn->value_count || addr >= fn->value_count ||
            !minic_type_pointee(fn->values[addr].type, &pointee) ||
            !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
            !minic_x86_64_storage_width(program, target, pointee, &bytes, &sign))
            return false;
        /* Naturally aligned 1/2/4/8-byte scalar MOV writes are atomic on
         * x86-64. Only relaxed semantics are implemented: sequentially
         * consistent operations require stronger target instructions. */
        return x86_head_load_memory_address(out, fn, frame, addr) &&
               x86_value_load(out, frame, src, "%rax") &&
               x86_store_memory(out, bytes);
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId src = ins->value.store.stored_value;
        if (src >= fn->value_count) return false;
        if (minic_type_is_long_double(fn->values[src].type))
            return x86_head_load_memory_address(out, fn, frame, ins->value.store.address) &&
                   x86_x87_load(out, frame, src) &&
                   fprintf(out, "  fstpt (%%r10)\n") >= 0;
        if (minic_type_is_int128_integer(fn->values[src].type))
            return x86_head_load_memory_address(out, fn, frame, ins->value.store.address) &&
                   x86_i128_load(out, frame, src, "%rax", "%rdx") &&
                   fprintf(out, "  movq %%rax, (%%r10)\n"
                                "  movq %%rdx, 8(%%r10)\n") >= 0;
        if (src >= fn->value_count ||
            !minic_x86_64_storage_width(program, target, fn->values[src].type, &bytes, &sign))
            return false;
        if (frame->direct_object_address_consumer &&
            frame->direct_object_address_value_id == ins->value.store.address) {
            char memory[64];
            return x86_object_memory_operand(frame,
                       frame->direct_object_address_object_id,
                       memory, sizeof(memory)) &&
                   x86_value_load(out, frame, src, "%rax") &&
                   x86_store_memory_at(out, bytes, memory);
        }
        return x86_head_load_memory_address(out, fn, frame, ins->value.store.address) &&
               x86_value_load(out, frame, src, "%rax") &&
               x86_store_memory(out, bytes);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicCoreValueId lhs = ins->value.integer_overflow.left;
        MinicCoreValueId rhs = ins->value.integer_overflow.right;
        MinicCoreValueId address = ins->value.integer_overflow.result_address;
        size_t width;
        bool signed_result;
        const char *opcode = NULL, *flag = NULL;
        if (!minic_core_integer_overflow_shape(
                program, target, fn, ins, &width, &signed_result)) {
            bool signed_left;
            if (!minic_core_integer_overflow_mixed_add_i32_shape(
                    program, target, fn, ins, &signed_left))
                return x86_emit_mixed_integer_overflow(
                    out, program, target, fn, frame, ins);
            if (!x86_value_load(out, frame, lhs, "%rax") ||
                !x86_value_load(out, frame, rhs, "%r10"))
                return false;
            /* GNU generic addition has infinite precision: unsigned64 plus
             * signed32 can require 65 bits. Keep a signed high limb, carry
             * from the low limb, and compare against sign-extended int32. */
            if (fputs(signed_left
                    ? "  movslq %eax, %rax\n  movq %rax, %r8\n"
                      "  sarq $63, %r8\n"
                    : "  movslq %r10d, %r10\n  movq %r10, %r8\n"
                      "  sarq $63, %r8\n", out) == EOF ||
                fputs("  addq %r10, %rax\n  adcq $0, %r8\n"
                      "  movslq %eax, %r9\n  cmpq %r9, %rax\n"
                      "  setne %r11b\n  sarq $63, %r9\n"
                      "  cmpq %r9, %r8\n  setne %r8b\n"
                      "  orb %r8b, %r11b\n", out) == EOF ||
                !x86_value_load(out, frame, address, "%r10") ||
                fputs("  movl %eax, (%r10)\n"
                      "  movzbq %r11b, %rax\n", out) == EOF)
                return false;
            return x86_value_store(out, frame, ins->result);
        }
        switch (ins->value.integer_overflow.operator_kind) {
        case MINIC_CORE_INTEGER_OVERFLOW_ADD:
            opcode = width == 8U ? "addq" : "addl";
            flag = signed_result ? "seto" : "setc";
            break;
        case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
            opcode = width == 8U ? "subq" : "subl";
            flag = signed_result ? "seto" : "setc";
            break;
        case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
            if (signed_result) opcode = width == 8U ? "imulq" : "imull";
            else opcode = width == 8U ? "mulq" : "mull";
            flag = "seto"; /* MUL/IMUL set OF iff the truncated result loses bits. */
            break;
        default:
            return false;
        }
        if (!x86_value_load(out, frame, lhs, "%rax") ||
            !x86_value_load(out, frame, rhs, "%r10"))
            return false;
        if (ins->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY &&
            !signed_result) {
            if (fprintf(out, width == 8U
                            ? "  mulq %%r10\n" : "  mull %%r10d\n") < 0)
                return false;
        } else if (fprintf(out,
                           width == 8U ? "  %s %%r10, %%rax\n"
                                       : "  %s %%r10d, %%eax\n", opcode) < 0)
            return false;
        /* Snapshot the arithmetic flag before any later arithmetic.
         * Two-address result memory is written before returning the bool. */
        if (fprintf(out, "  %s %%r11b\n", flag) < 0 ||
            !x86_value_load(out, frame, address, "%r10") ||
            fprintf(out, width == 8U
                           ? "  movq %%rax, (%%r10)\n"
                           : "  movl %%eax, (%%r10)\n") < 0 ||
            fprintf(out, "  movzbq %%r11b, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        const MinicCoreValueId operand = ins->value.operand;
        unsigned int width;
        /* Baseline x86-64 BSR/BSF avoid BMI1/LZCNT feature assumptions.
         * The Core operand is the promoted unsigned integer type, not
         * necessarily 64 bits: clz(unsigned int) must return 31 for 1. */
        if (!minic_core_integer_bitscan_shape(
                program, target, fn, ins, &width) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ) {
            if (fprintf(out,
                "  testq %%rax, %%rax\n"
                "  jz 1f\n"
                "  bsrq %%rax, %%r10\n"
                "  movl $%u, %%eax\n"
                "  subl %%r10d, %%eax\n"
                "  jmp 2f\n"
                "1:\n"
                "  movl $%u, %%eax\n"
                "2:\n", width - 1U, width) < 0) return false;
        } else if (fprintf(out,
                "  testq %%rax, %%rax\n"
                "  jz 1f\n"
                "  bsfq %%rax, %%rax\n"
                "  jmp 2f\n"
                "1:\n"
                "  movl $%u, %%eax\n"
                "2:\n", width) < 0) return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (ins->value.operand >= fn->value_count) return false;
        if (minic_type_is_int128_integer(fn->values[ins->value.operand].type)) {
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) return false;
            if (!x86_i128_load(out, frame, ins->value.operand, "%rax", "%rdx"))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
                if (fprintf(out, "  negq %%rax\n  adcq $0, %%rdx\n  negq %%rdx\n") < 0)
                    return false;
            } else if (fprintf(out, "  notq %%rax\n  notq %%rdx\n") < 0)
                return false;
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!x86_value_load(out, frame, ins->value.operand, "%rax")) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            if (fprintf(out, "  negq %%rax\n") < 0) return false;
        } else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) {
            if (fprintf(out, "  notq %%rax\n") < 0) return false;
        } else if (fprintf(out, "  testq %%rax, %%rax\n  sete %%al\n  movzbq %%al, %%rax\n") < 0)
            return false;
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        if (left >= fn->value_count || right >= fn->value_count)
            return false;
        if (minic_type_is_int128_integer(fn->values[left].type)) {
            /* 128-bit x86 registers are pairs of 64-bit machine registers.
             * Only explicitly handled operations are admitted. */
            if (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_ADD &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT &&
                ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
                ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL)
                return false;
            if (!x86_i128_load(out, frame, left, "%rax", "%rdx"))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
                if (minic_type_is_int128_integer(fn->values[right].type) ||
                    !x86_value_load(out, frame, right, "%rcx"))
                    return false;
                /* Shift by >=128 is undefined C behavior. The >=64 limb case
                 * is needed by SQLite's high-64 and high-32 extraction. */
                if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
                    bool signed_value = minic_type_is_signed_integer(ins->type);
                    if (fprintf(out,
                        "  cmpq $64, %%rcx\n"
                        "  jae 1f\n"
                        "  shrdq %%cl, %%rdx, %%rax\n"
                        "  %s %%cl, %%rdx\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  subq $64, %%rcx\n"
                        "  movq %%rdx, %%rax\n"
                        "  %s %%cl, %%rax\n"
                        "  %s\n"
                        "2:\n",
                        signed_value ? "sarq" : "shrq",
                        signed_value ? "sarq" : "shrq",
                        signed_value ? "  sarq $63, %rdx" : "  xorq %rdx, %rdx") < 0)
                        return false;
                } else if (fprintf(out,
                        "  cmpq $64, %%rcx\n"
                        "  jae 1f\n"
                        "  shldq %%cl, %%rax, %%rdx\n"
                        "  shlq %%cl, %%rax\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  subq $64, %%rcx\n"
                        "  movq %%rax, %%rdx\n"
                        "  shlq %%cl, %%rdx\n"
                        "  xorq %%rax, %%rax\n"
                        "2:\n") < 0)
                    return false;
                return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
            }
            if (!minic_type_is_int128_integer(fn->values[right].type) ||
                !x86_i128_load(out, frame, right, "%r10", "%r11"))
                return false;
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
                if (fprintf(out, "  addq %%r10, %%rax\n  adcq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
                if (fprintf(out, "  subq %%r10, %%rax\n  sbbq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
                if (fprintf(out,
                    "  movq %%rdx, %%r8\n"
                    "  imulq %%r10, %%r8\n"
                    "  movq %%r11, %%r9\n"
                    "  imulq %%rax, %%r9\n"
                    "  mulq %%r10\n"
                    "  addq %%r8, %%rdx\n"
                    "  addq %%r9, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
                if (fprintf(out, "  andq %%r10, %%rax\n  andq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
                if (fprintf(out, "  orq %%r10, %%rax\n  orq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
                if (fprintf(out, "  xorq %%r10, %%rax\n  xorq %%r11, %%rdx\n") < 0)
                    return false;
                break;
            case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
                /* Compare high words with source signedness; when high
                 * words match, compare the low words unsigned. Do the low
                 * comparison first to avoid clobbering the input with setcc. */
                if (fprintf(out,
                    "  cmpq %%r10, %%rax\n"
                    "  setb %%cl\n"
                    "  cmpq %%r11, %%rdx\n"
                    "%s"
                    "  sete %%dl\n"
                    "  andb %%cl, %%dl\n"
                    "  orb %%dl, %%al\n"
                    "  movzbq %%al, %%rax\n",
                    minic_type_is_signed_integer(fn->values[left].type)
                      ? "  setl %al\n" : "  setb %al\n") < 0)
                    return false;
                return x86_value_store(out, frame, ins->result);
            case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
                if (fprintf(out,
                    "  cmpq %%r10, %%rax\n"
                    "  sete %%cl\n"
                    "  cmpq %%r11, %%rdx\n"
                    "  sete %%al\n"
                    "  andb %%cl, %%al\n"
                    "  movzbq %%al, %%rax\n") < 0)
                    return false;
                return x86_value_store(out, frame, ins->result);
            default: return false;
            }
            return x86_i128_store(out, frame, ins->result, "%rax", "%rdx");
        }
        if (!x86_value_load(out, frame, left, "%rax") ||
            !x86_value_load(out, frame, right, "%r10")) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
            if (fprintf(out, "  addq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
            if (fprintf(out, "  subq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            if (fprintf(out, "  imulq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, sign ? "  cqto\n  idivq %%r10\n" :
                                    "  xorq %%rdx, %%rdx\n  divq %%r10\n") < 0)
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER &&
                fprintf(out, "  movq %%rdx, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
            if (fprintf(out, "  andq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
            if (fprintf(out, "  orq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
            if (fprintf(out, "  xorq %%r10, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
            if (fprintf(out, "  movb %%r10b, %%cl\n  shlq %%cl, %%rax\n") < 0) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, sign ? "  movb %%r10b, %%cl\n  sarq %%cl, %%rax\n"
                                  : "  movb %%r10b, %%cl\n  shrq %%cl, %%rax\n") < 0)
                return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
            if (!minic_x86_64_scalar_width(program, target, fn->values[left].type, &bytes, &sign) ||
                fprintf(out, "  cmpq %%r10, %%rax\n") < 0) return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) {
                if (fprintf(out, "  sete %%al\n") < 0) return false;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS || !sign) {
                if (fprintf(out, "  setb %%al\n") < 0) return false;
            } else if (fprintf(out, "  setl %%al\n") < 0) return false;
            if (fprintf(out, "  movzbq %%al, %%rax\n") < 0) return false;
            break;
        default: return false;
        }
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return x86_value_load(out, frame, ins->value.field_address.base, "%rax") &&
               fprintf(out, "  addq $%zu, %%rax\n", ins->value.field_address.byte_offset) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!x86_value_load(out, frame, ins->value.pointer_offset.base, "%rax") ||
            !x86_value_load(out, frame, ins->value.pointer_offset.index, "%r10") ||
            fprintf(out, "  imulq $%zu, %%r10, %%r10\n",
                    ins->value.pointer_offset.element_size) < 0 ||
            fprintf(out, ins->value.pointer_offset.subtract
                         ? "  subq %%r10, %%rax\n" : "  addq %%r10, %%rax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        MinicCoreValueId count = ins->value.operand;
        /* The current dynamically aligned object frame is rsp-relative.
         * Alloca changes rsp, so reject this unqualified combination
         * rather than silently addressing the wrong local object. */
        if (frame->objects_from_aligned_rsp ||
            ins->result >= fn->value_count || count >= fn->value_count ||
            !minic_type_is_pointer(ins->type) ||
            !minic_type_equal(fn->values[ins->result].type, ins->type) ||
            !minic_type_equal(fn->values[count].type, minic_type_unsigned_long()) ||
            !x86_value_load(out, frame, count, "%r10") ||
            fprintf(out,
                    "  addq $15, %%r10\n"
                    "  jnc 1f\n"
                    "  ud2\n"
                    "1:\n"
                    "  andq $-16, %%r10\n"
                    "  subq %%r10, %%rsp\n"
                    "  movq %%rsp, %%rax\n") < 0)
            return false;
        /* New rsp stays 16-byte aligned, so subsequent SysV calls may
         * reserve/reclaim outgoing argument space normally. Epilogue
         * uses leave to reclaim every dynamic allocation at once. */
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    default:
        return false;
    }
}

bool minic_x86_64_emit_core_terminator(FILE *out,
                                const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicX86Frame *frame,
                                const MinicCoreTerminator *term,
                                const MinicCoreFunction *fn, size_t function_id) {
    if (term == NULL) return false;
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (term->return_object != MINIC_CORE_OBJECT_INVALID) {
            size_t bytes;
            if (!program || !target || !frame || !fn ||
                term->return_value != MINIC_CORE_VALUE_INVALID ||
                term->return_object >= fn->object_count ||
                !minic_type_is_record(fn->return_type) ||
                !minic_x86_64_record_abi_compatible(
                    fn->return_type, fn->objects[term->return_object].type) ||
                !x86_object_address(out, frame, term->return_object, "%r11"))
                return false;
            if (minic_x86_64_one_gp_record(program, target, fn->return_type, &bytes)) {
                if (!x86_one_gp_load(out,bytes)) return false;
            } else if (minic_x86_64_two_gp_record(program, target, fn->return_type)) {
                const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
                    MINIC_CALLING_CONVENTION_DEFAULT);
                if (abi == NULL || abi->gp_result_count < 2U ||
                    fprintf(out, "  movq 0(%%r11), %s\n"
                                 "  movq 8(%%r11), %s\n",
                            abi->gp_results[0], abi->gp_results[1]) < 0)
                    return false;
            } else if (frame->has_sret && frame->sret_save_offset != 0U &&
                       minic_x86_64_memory_result_record(
                           program, target, fn->return_type, &bytes)) {
                /* Caller passes a private destination in RDI. Its saved
                 * value is stable across nested calls; a bounded byte copy
                 * preserves tail bytes and does not read past the record. */
                if (fprintf(out,
                    "  movq %%r11, %%rsi\n"
                    "  movq -%zu(%%rbp), %%rdi\n"
                    "  movq $%zu, %%rcx\n"
                    "  rep movsb\n"
                    "  movq -%zu(%%rbp), %%rax\n",
                    frame->sret_save_offset, bytes,
                    frame->sret_save_offset) < 0) return false;
            } else {
                /* Unclassified SSE or mixed SysV results remain rejected;
                 * never return a record using an invented scalar ABI. */
                return false;
            }
            return fprintf(out,"  jmp .Lmini_x86_f%zu_ret\n",function_id)>=0;
        }
        if (term->return_value != MINIC_CORE_VALUE_INVALID) {
            if (minic_type_is_long_double(fn->return_type))
                return x86_x87_load(out, frame, term->return_value) &&
                       fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
            if (minic_type_is_int128_integer(fn->return_type)) {
                const MinicX86AbiRegisters *abi = minic_x86_64_abi_registers(
                    MINIC_CALLING_CONVENTION_DEFAULT);
                return abi != NULL && abi->gp_result_count >= 2U &&
                       x86_i128_load(out, frame, term->return_value,
                                     abi->gp_results[0], abi->gp_results[1]) &&
                       fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
            }
            if (!x86_value_load(out, frame, term->return_value, "%rax"))
                return false;
            if (minic_type_is_double(fn->return_type)) {
                if (fprintf(out, "  movq %%rax, %%xmm0\n") < 0) return false;
            } else if (minic_type_is_float(fn->return_type)) {
                if (fprintf(out, "  movd %%eax, %%xmm0\n") < 0) return false;
            }
        }
        return fprintf(out, "  jmp .Lmini_x86_f%zu_ret\n", function_id) >= 0;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        /* Lua lvm.c uses GNU computed-goto dispatch. Core supplies an SSA
         * code pointer; only target instruction selection differs from RV64. */
        return x86_value_load(out, frame, term->indirect_target, "%rax") &&
               ((target->x86_codegen_options &
                 MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U
                    ? fprintf(out, "  jmp __x86_indirect_thunk_rax\n") >= 0
                    : (target->x86_codegen_options &
                       MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK) != 0U
                        ? x86_emit_indirect_inline_thunk(out, "%rax", false)
                    : fprintf(out, "  jmp *%%rax\n") >= 0);
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(out, "  jmp .Lmini_x86_f%zu_b%u\n",
                       function_id, (unsigned int)term->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return x86_value_load(out, frame, term->conditional.condition, "%rax") &&
               fprintf(out, "  testq %%rax, %%rax\n"
                            "  jne .Lmini_x86_f%zu_b%u\n"
                            "  jmp .Lmini_x86_f%zu_b%u\n",
                       function_id, (unsigned int)term->conditional.when_true,
                       function_id, (unsigned int)term->conditional.when_false) >= 0;
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(out, "  ud2\n") >= 0;
    default: return false;
    }
}


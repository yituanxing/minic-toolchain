#ifndef MINIC_CORE_CORE_IR_H
#define MINIC_CORE_CORE_IR_H

#include "common/source_location.h"
#include "core/core_type.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t MinicCoreValueId;
typedef uint32_t MinicCoreInstructionId;
typedef uint32_t MinicCoreBlockId;
typedef uint32_t MinicCoreObjectId;
typedef uint32_t MinicCoreGlobalId;
typedef uint32_t MinicCoreFunctionSymbolId;
typedef uint32_t MinicCoreCalleeId;
typedef uint32_t MinicCoreCallSignatureId;
typedef uint32_t MinicCoreInlineAsmId;

#define MINIC_CORE_VALUE_INVALID UINT32_MAX
#define MINIC_CORE_INSTRUCTION_INVALID UINT32_MAX
#define MINIC_CORE_BLOCK_INVALID UINT32_MAX
#define MINIC_CORE_OBJECT_INVALID UINT32_MAX
#define MINIC_CORE_GLOBAL_INVALID UINT32_MAX
#define MINIC_CORE_FUNCTION_SYMBOL_INVALID UINT32_MAX
#define MINIC_CORE_CALLEE_INVALID UINT32_MAX
#define MINIC_CORE_CALL_SIGNATURE_INVALID UINT32_MAX
#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX

typedef enum MinicCoreIntegerOverflowOperator {
    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,
    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT,
    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY
} MinicCoreIntegerOverflowOperator;

/* M79_CALL_FRAME_RETURN_ADDRESS: target-neutral semantic origin for
   GNU call-frame address builtins. Backends may support only a subset of
   kind/level pairs; unsupported pairs remain fail-closed. */
typedef enum MinicCoreCallFrameAddressKind {
    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN = 0,
    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME
} MinicCoreCallFrameAddressKind;

/* A FIELD_ADDRESS names one nominal Core record field. The resulting address
 * denotes the complete field type, except for a packed bit-field's scalar
 * storage carrier. Array elements require explicit typed address lowering. */
typedef enum MinicCoreFieldAccessForm {
    MINIC_CORE_FIELD_ACCESS_EXACT = 0,
    /* Numeric form 1 belonged to the retired legacy array-element shortcut.
     * Keep the remaining encoding stable and reject form 1 at verification. */
    MINIC_CORE_FIELD_ACCESS_BITFIELD_STORAGE = 2
} MinicCoreFieldAccessForm;

/* M158_FINAL_STRICT_TAIL */
typedef enum MinicCoreInstructionKind {
    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,
    /* M175B_SCALAR_DOUBLE_BRIDGE: raw IEEE-754 double payload. */
    MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT,
    MINIC_CORE_INSTRUCTION_INTEGER_ADD,
    MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT,
    MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,
    MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE,
    MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR,
    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT,
    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT,
    MINIC_CORE_INSTRUCTION_INTEGER_LESS,
    MINIC_CORE_INSTRUCTION_POINTER_LESS,
    MINIC_CORE_INSTRUCTION_SCALAR_EQUAL,
    MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW,
    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,
    MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE,
    MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER,
    MINIC_CORE_INSTRUCTION_SCALAR_BITCAST,
    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,
    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,
    /* Target-neutral integer bit-count primitives. */
    MINIC_CORE_INSTRUCTION_INTEGER_CLZ,
    MINIC_CORE_INSTRUCTION_INTEGER_CTZ,
    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,
    MINIC_CORE_INSTRUCTION_PARAMETER,
    MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ,
    MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT,
    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,
    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,
    /* M81_FUNCTION_ADDRESS_VALUE: first-class address of a function symbol. */
    MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS,
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: target-neutral address of a Core basic block. */
    MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS,
    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,
    MINIC_CORE_INSTRUCTION_POINTER_OFFSET,
    MINIC_CORE_INSTRUCTION_LOAD,
    MINIC_CORE_INSTRUCTION_STORE,
    /* BATCH_M_RECORD_LOAD: materialize one address-backed aggregate value into
       a private Core object while preserving source volatility. */
    MINIC_CORE_INSTRUCTION_RECORD_LOAD,
    /* M80_ADDRESS_BACKED_RECORD_COPY: byte-preserving aggregate memory copy. */
    MINIC_CORE_INSTRUCTION_RECORD_COPY,
    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,
    /* Four retired scalar-ASM opcode numbers remain unassigned. Preserve the
     * numeric identity of all subsequent Core instructions without retaining
     * a parallel instruction representation or accepting their old encodings. */
    MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM + 5,
    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER,
    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,
    /* M123_VARIADIC_ARGUMENT_ADDRESS: semantic origin of a va_list cursor.
       Backend ABI owns register-save-area placement and the concrete address. */
    MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS,
    MINIC_CORE_INSTRUCTION_CALL,
    /* M83_FIRST_CLASS_INDIRECT_CALL: callee is an SSA function-pointer value. */
    MINIC_CORE_INSTRUCTION_INDIRECT_CALL,
    /* RUNTIME_R0_SCALAR_DOUBLE: target-neutral binary64 arithmetic. Append
       after the established opcode range so existing kind numbers stay stable. */
    MINIC_CORE_INSTRUCTION_DOUBLE_ADD,
    MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT,
    MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY,
    MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE,
    /* RUNTIME_R0_DOUBLE_COMPARE: preserve ordered IEEE-754 comparison
       semantics explicitly.  In particular >= is not modeled as !(<). */
    MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL,
    MINIC_CORE_INSTRUCTION_DOUBLE_LESS,
    MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL,
    MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE,
    /* RUNTIME_R0_FLOAT_TRANSPORT: float participates only as a transported
       32-bit scalar here; arithmetic remains binary64-owned until separately
       admitted. */
    MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE,
    MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT,
    /* RUNTIME_R0_SCALAR_FLOAT: true binary32 arithmetic. Keep these appended
       so previously established Core instruction kind numbers remain stable. */
    MINIC_CORE_INSTRUCTION_FLOAT_ADD,
    MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT,
    MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY,
    MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE,
    /* Runtime stack allocation. The byte-count operand is an unsigned integer;
       the result is a pointer with lifetime through the containing function. */
    MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC,
    /* SysV va_arg is a target-selected cursor read: Core retains the typed
     * semantic operation, not RV64's eight-byte linear cursor arithmetic. */
    MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR,
    /* Target-neutral atomic write; memory order remains explicit. */
    MINIC_CORE_INSTRUCTION_ATOMIC_STORE,
    MINIC_CORE_INSTRUCTION_ATOMIC_LOAD,
    /* va_copy is a value copy of the target's ABI cursor state, NOT
     * assignment of the frontend's pointer-shaped va_list proxy.
     * Both Core operands are pointer SSA values; target copies its real tag. */
    MINIC_CORE_INSTRUCTION_VARIADIC_COPY,
    /* Target-neutral effect, register destination identity plus SSA source.
     * Append after frozen enum values to preserve established IR numbering. */
    MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE,
    /* Address-backed vector lvalue storage copy: no register value or call ABI. */
    MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY,
    /* True 128-bit typed vector value operations; never scalar/record copy.
     * ISA permission, V-register ABI and exact intrinsics remain target owned. */
    MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD,
    MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE,
    MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR
} MinicCoreInstructionKind;

/* One exclusive upper bound for opcode-indexed tables and diagnostics.
 * Keep this adjacent to the last real opcode as the IR vocabulary grows;
 * it is intentionally not an instruction-kind enum member. */
enum { MINIC_CORE_INSTRUCTION_KIND_COUNT = MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR + 1U };

/* M91_BUILTIN_UNREACHABLE_TERMINATOR: unreachable is a CFG fact, not a
   value-producing instruction and not an invented target trap. */
typedef enum MinicCoreTerminatorKind {
    MINIC_CORE_TERMINATOR_RETURN = 0,
    MINIC_CORE_TERMINATOR_BRANCH,
    /* M158_FINAL_STRICT_TAIL: GNU computed goto consumes a pointer value. */
    MINIC_CORE_TERMINATOR_INDIRECT_BRANCH,
    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH,
    MINIC_CORE_TERMINATOR_UNREACHABLE
} MinicCoreTerminatorKind;

typedef struct MinicCoreValue {
    /* One definition owns the source draft type during C lowering. A sealed
     * Core SSA value carries no duplicate C descriptor. */
    MinicCoreTypeId core_type_id;
    MinicCoreInstructionId definition;
} MinicCoreValue;

/* Fixed-register spelling is target metadata, but ownership belongs to Core
   once lowering commits the function. Backends consume this opaque binding
   instead of reopening the frontend Program side table. */
typedef struct MinicCoreFixedRegisterBinding {
    char *register_name;
    size_t register_name_length;
    MinicCoreTypeId core_type_id;
    bool is_local;
} MinicCoreFixedRegisterBinding;

typedef struct MinicCoreObject {
    MinicSourceSpan span;
    /* The entire storage extent is owned by CoreType. A repeated object is
     * represented by a first-class Core array, not an extra object count. */
    MinicCoreTypeId core_type_id;
    /* Zero means natural DataLayout alignment. Nonzero alignment is captured
       from source object semantics during AST -> Core lowering. */
    size_t explicit_alignment;
} MinicCoreObject;

typedef struct MinicCoreGlobal {
    char *name;
    size_t name_length;
    MinicCoreTypeId core_type_id;
} MinicCoreGlobal;

typedef struct MinicCoreFunctionSymbol {
    char *name;
    size_t name_length;
} MinicCoreFunctionSymbol;

typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    /* One nominal Core FUNCTION owns the executable direct-call contract.
     * The name is independent: synthetic libcalls need no Module declaration. */
    MinicCoreTypeId core_function_type_id;
} MinicCoreCallee;

/* M83_FIRST_CLASS_INDIRECT_CALL: one nominal Core FUNCTION identity owns the
   entire indirect-call contract (return, formals, varargs and convention).
   Call planners resolve these facts through the sealed Core type arena. */
typedef struct MinicCoreCallSignature {
    MinicCoreTypeId core_function_type_id;
} MinicCoreCallSignature;

/* M85_RECORD_CALL_ARGUMENT: Core call arguments carry semantic storage form,
   not target ABI locations. Scalar arguments are SSA values; aggregate
   by-value arguments are immutable snapshots in Core objects. */
typedef enum MinicCoreCallArgumentKind {
    MINIC_CORE_CALL_ARGUMENT_INVALID = 0,
    MINIC_CORE_CALL_ARGUMENT_VALUE,
    MINIC_CORE_CALL_ARGUMENT_OBJECT
} MinicCoreCallArgumentKind;

typedef struct MinicCoreCallArgument {
    MinicCoreCallArgumentKind kind;
    union {
        MinicCoreValueId value_id;
        MinicCoreObjectId object_id;
    } value;
} MinicCoreCallArgument;

/* An extended-precision numerical operation is a typed Core semantic, not
 * a CALL recognized by the spelling of a GNU compiler-runtime symbol.
 * Physical x87/binary128 lowering and the fallback runtime ABI are chosen by
 * the target; ordinary external calls with identical symbol names remain
 * ordinary calls and never inherit these operation semantics. */
typedef enum MinicCoreExtendedFloatOperation {
    MINIC_CORE_EXT_FLOAT_NONE = 0,
    MINIC_CORE_EXT_FLOAT_ADD,
    MINIC_CORE_EXT_FLOAT_SUBTRACT,
    MINIC_CORE_EXT_FLOAT_MULTIPLY,
    MINIC_CORE_EXT_FLOAT_DIVIDE,
    MINIC_CORE_EXT_FLOAT_NEGATE,
    /* Ordered predicates produce canonical i32 0/1, including for NaNs.
     * They do not expose a target compiler-runtime comparison status. */
    MINIC_CORE_EXT_FLOAT_EQUAL,
    MINIC_CORE_EXT_FLOAT_LESS,
    MINIC_CORE_EXT_FLOAT_LESS_EQUAL,
    MINIC_CORE_EXT_FLOAT_FROM_BINARY32,
    MINIC_CORE_EXT_FLOAT_FROM_BINARY64,
    MINIC_CORE_EXT_FLOAT_TO_BINARY32,
    MINIC_CORE_EXT_FLOAT_TO_BINARY64,
    MINIC_CORE_EXT_FLOAT_FROM_SIGNED32,
    MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED32,
    MINIC_CORE_EXT_FLOAT_FROM_SIGNED64,
    MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED64,
    MINIC_CORE_EXT_FLOAT_TO_SIGNED32,
    MINIC_CORE_EXT_FLOAT_TO_UNSIGNED32,
    MINIC_CORE_EXT_FLOAT_TO_SIGNED64,
    MINIC_CORE_EXT_FLOAT_TO_UNSIGNED64
} MinicCoreExtendedFloatOperation;

typedef struct MinicCoreInlineAsmRegisterClobber {
    char *name;
    size_t name_length;
} MinicCoreInlineAsmRegisterClobber;

/* Source/Generic/Target admission is a semantic Core provenance decision,
 * not an ISA type. The target validates actual machine opcode forms. */
typedef enum MinicAsmOperandPlanOwner {
    MINIC_ASM_OPERAND_PLAN_NONE = 0,
    MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED,
    MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED
} MinicAsmOperandPlanOwner;

/* An emitted Core ASM effect can reference a linker-visible name even when
 * its operand is an immediate rather than an ordinary Core address value.
 * The spelling and byte displacement are immutable, owned Core facts; no
 * source ExpressionId or target relocation policy crosses this boundary. */
typedef enum MinicCoreInlineAsmSymbolKind {
    MINIC_CORE_INLINE_ASM_SYMBOL_FUNCTION = 0,
    MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT
} MinicCoreInlineAsmSymbolKind;

typedef struct MinicCoreInlineAsmSymbolReference {
    MinicCoreInlineAsmSymbolKind kind;
    char *name;
    size_t name_length;
    int64_t byte_addend;
} MinicCoreInlineAsmSymbolReference;

/* GNU `i` operands cross into Core as a proven constant or one ELF symbol
 * plus a signed BYTE displacement. Machine stages must not recover types by
 * reparsing source-formatted decimal or assembler relocation expressions. */
typedef enum MinicCoreInlineAsmImmediateKind {
    MINIC_CORE_INLINE_ASM_IMMEDIATE_INVALID = 0,
    MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER,
    MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL
} MinicCoreInlineAsmImmediateKind;

typedef struct MinicCoreInlineAsmImmediate {
    MinicCoreInlineAsmImmediateKind kind;
    int64_t integer;
    char *symbol;
    int64_t byte_addend;
} MinicCoreInlineAsmImmediate;

/* A precise ELF name or name+byte-addend witness; no assembler syntax. The
 * formatter is for final target output, never a semantic parsing step. */
static inline bool minic_core_inline_asm_symbol_valid(const char *symbol) {
    size_t i, length;
    if (symbol == NULL) return false;
    length = strlen(symbol);
    if (length == 0U || length > 128U ||
        !((symbol[0] >= 'a' && symbol[0] <= 'z') ||
          (symbol[0] >= 'A' && symbol[0] <= 'Z') ||
          symbol[0] == '_' || symbol[0] == '.')) return false;
    for (i = 1U; i < length; ++i) {
        const char ch = symbol[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
            ch == '$') continue;
        return false;
    }
    return true;
}

static inline bool minic_core_inline_asm_relocation_valid(
    const MinicCoreInlineAsmImmediate *immediate) {
    uint64_t magnitude;
    size_t length, digits = 1U;
    if (immediate == NULL ||
        immediate->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL ||
        immediate->integer != 0 ||
        !minic_core_inline_asm_symbol_valid(immediate->symbol)) return false;
    length = strlen(immediate->symbol);
    if (immediate->byte_addend == 0) return true;
    magnitude = immediate->byte_addend < 0
        ? (uint64_t)(-(immediate->byte_addend + 1)) + 1U
        : (uint64_t)immediate->byte_addend;
    while (magnitude >= 10U) {
        magnitude /= 10U;
        ++digits;
    }
    /* One signed byte displacement, bounded without rendering any target
     * expression. The assembler owns its eventual syntax. */
    return digits < 128U && length <= 128U - 1U - digits;
}

typedef struct MinicCoreInlineAsm {
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;
    /* Immutable typed source-admission result. The source-form fingerprint
     * remains entirely in the C ingress; machine emission consumes this
     * normalized operand/effect contract and its target-owned form ID. */
    MinicAsmOperandPlanOwner operand_plan_owner;
    /* Opaque target-produced ASM dispatch facts. A source hash is only
     * provenance; it must never be interpreted as this machine form ID. */
    uint32_t target_machine_form_id;
    uint32_t target_machine_form_variant;
    bool explicit_memory_clobber;
    bool implicit_memory_effect;
    /* BATCH_L_STRUCTURED_REGISTER_READWRITE: keep register-clobber spelling
       as opaque target metadata. Core does not interpret register names; the
       selected backend only uses them to avoid operand/clobber collisions. */
    MinicCoreInlineAsmRegisterClobber *register_clobbers;
    size_t register_clobber_count;
    size_t register_clobber_capacity;
    /* M76_SINGLE_LABEL_ASM_GOTO: preserve the control-flow target in Core
       instead of hiding it inside target assembly text. The first supported
       seam is one label plus one deferred immediate input. */
    bool is_goto;
    /* Immutable machine-local relocation symbol for an unresolved `asm goto`
     * `%0` immediate. NULL if every immediate has already been resolved;
     * non-goto structured ASM dispatch uses its sealed machine-form ID and
     * must not retain a borrowed source Program statement identity. */
    char *deferred_immediate_symbol;
    MinicCoreBlockId goto_target;
    /* Second CFG destination for a TARGET-qualified multi-label asm-goto.
     * Existing one-label forms leave this as INVALID. */
    MinicCoreBlockId goto_second_target;
    /* Immutable typed numeric constants or relocation witnesses; target
     * emission formats machine syntax exactly once at its final boundary. */
    MinicCoreInlineAsmImmediate resolved_immediates[3];
    size_t resolved_immediate_count;
    /* These references root definitions only when the corresponding Core
     * instruction survives the post-lowering reachable-block closure. */
    MinicCoreInlineAsmSymbolReference *symbol_references;
    size_t symbol_reference_count;
    size_t symbol_reference_capacity;
    /* Source operands with uninspectable nested expressions may reference
     * static storage; preserve it only when this exact ASM effect is live. */
    bool preserve_all_static_objects;
} MinicCoreInlineAsm;

/* Core-wide bound for GNU extended-asm operand roles. Keep this independent
 * from the smaller operand counts of ISA-specific machine templates. */
#define MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT 16U

typedef enum MinicCoreStructuredInlineAsmOperandKind {
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT = 0,
    /* A register read/write operand is address-backed: load the lvalue before
       asm, bind one target register, then store the post-asm value back. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,
    /* M107_STRUCTURED_MEMORY_OUTPUT_ASM: address-backed write-only memory
       operand (`=m`). Keep this distinct from read/write memory (`+m`/`+A`). */
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
    /* M125_STRUCTURED_MEMORY_INPUT_ASM: read-only `m` operands carry an
       address into Core, but unlike output/read-write memory they permit const
       pointees and never require post-asm writeback. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
    /* An SSA register output, unlike REGISTER_OUTPUT's address-backed lvalue.
     * The sole result lives in instruction->result; operand.value is INVALID.
     * This allows a mixed +A memory output and =r result to retain the
     * original GNU operand positions without adding a synthetic Core store. */
    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT
} MinicCoreStructuredInlineAsmOperandKind;

typedef struct MinicCoreStructuredInlineAsmOperand {
    MinicCoreStructuredInlineAsmOperandKind kind;
    size_t operand_index;
    MinicCoreValueId value;
    /* M105_FIXED_REGISTER_STRUCTURED_ASM: keep frontend-owned local fixed-register
       identity as opaque metadata. Core does not interpret the register spelling;
       the selected backend resolves the binding when materializing asm operands. */
    size_t fixed_register_binding_id;
    bool has_fixed_register_binding;
    /* M126A_GENERIC_STRUCTURED_ASM: preserve GCC `&` early-clobber semantics
       as target-neutral scheduling/allocation metadata. */
    bool early_clobber;
    /* Normalized native physical-register early-clobber constraint. Unlike
     * `early_clobber` above this describes the native operand assignment,
     * not Core value/address transport. A target may represent `=&r` as a
     * Core MEMORY_OUTPUT (aggregate result) while still requiring a distinct
     * physical register from all live inputs. */
    bool physical_early_clobber;
    bool physical_read_write;
    bool has_tied_output;
    size_t tied_output_index;
} MinicCoreStructuredInlineAsmOperand;

typedef struct MinicCoreInstruction {
    MinicCoreInstructionKind kind;
    MinicSourceSpan span;
    MinicCoreTypeId core_type_id;
    MinicCoreValueId result;
    union {
        int64_t integer_value;
        uint64_t floating_bits;
        struct {
            uint64_t low;
            uint64_t high;
        } floating128_bits;
        struct {
            MinicCoreValueId left;
            MinicCoreValueId right;
        } binary;
        struct {
            MinicCoreIntegerOverflowOperator operator_kind;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreValueId result_address;
        } integer_overflow;
        MinicCoreValueId operand;
        /* An ABI cursor has independently addressable state. va_start
         * initializes it from the incoming register/stack banks, whereas
         * va_copy allocates an uninitialized destination which the copy
         * effect fills before exposing it to source-level consumers. */
        struct {
            bool copy_destination;
        } variadic_address;
        size_t parameter_index;
        size_t fixed_register_binding_id;
        struct {
            size_t binding_id;
            MinicCoreValueId source;
        } fixed_register_write;
        struct {
            MinicCoreCallFrameAddressKind kind;
            unsigned int level;
        } call_frame_address;
        struct {
            size_t parameter_index;
            MinicCoreObjectId object_id;
        } parameter_object;
        MinicCoreObjectId object_id;
        MinicCoreGlobalId global_id;
        MinicCoreFunctionSymbolId function_symbol_id;
        MinicCoreBlockId block_id;
        struct {
            MinicCoreValueId base;
            MinicCoreTypeId core_record_type_id;
            size_t field_index;
            MinicCoreFieldAccessForm access_form;
            /* Target-neutral DataLayout result captured at AST -> Core lowering.
               Backends consume the byte displacement without reopening record AST. */
            size_t byte_offset;
        } field_address;
        struct {
            MinicCoreValueId base;
            MinicCoreValueId index;
            size_t element_size;
            /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: preserve pointer -=
               as subtraction instead of negating a potentially unsigned index. */
            bool subtract;
        } pointer_offset;
        struct {
            MinicCoreValueId address;
            bool is_volatile;
        } load;
        struct {
            MinicCoreValueId address;
            MinicCoreValueId stored_value;
            bool is_volatile;
        } store;
        struct {
            MinicCoreValueId address;
            MinicCoreValueId stored_value;
            unsigned int memory_order;
        } atomic_store;
        struct {
            MinicCoreValueId address;
            unsigned int memory_order;
        } atomic_load;
        struct {
            MinicCoreValueId source_address;
            MinicCoreObjectId destination_object;
            bool is_volatile;
        } record_load;
        struct {
            MinicCoreValueId destination_address;
            MinicCoreValueId source_address;
            /* Explicit access effects, not inherited from a language type.
             * Both byte-copy sides may be volatile independently. */
            bool destination_volatile;
            bool source_volatile;
        } record_copy;
        struct {
            MinicCoreValueId destination_address;
            MinicCoreValueId source_address;
            bool destination_volatile;
            bool source_volatile;
        } vector_storage_copy;
        /* Target ABI state-copy: left=destination cursor pointer, right=source. */
        struct {
            MinicCoreValueId destination;
            MinicCoreValueId source;
        } variadic_copy;
        MinicCoreInlineAsmId inline_asm_id;
        struct {
            MinicCoreInlineAsmId inline_asm_id;
            size_t operand_count;
            MinicCoreStructuredInlineAsmOperand operands[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
        } structured_inline_asm;
        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
            /* NONE denotes a real external CALL. A nonzero tag owns the
             * numerical semantics and cannot be inferred from callee.name. */
            MinicCoreExtendedFloatOperation extended_float_operation;
            /* M86_DIRECT_RECORD_CALL_RESULT: aggregate call results remain
               address-backed Core objects rather than becoming aggregate SSA. */
            MinicCoreObjectId result_object;
        } call;
        struct {
            MinicCoreValueId callee;
            MinicCoreCallSignatureId signature_id;
            size_t argument_begin;
            size_t argument_count;
            /* Indirect and direct aggregate returns obey the same Core
             * ownership contract: result bytes live in an object, and
             * machine-specific ABI placement is deferred to Target. */
            MinicCoreObjectId result_object;
        } indirect_call;
    } value;
} MinicCoreInstruction;

/* Output roles are already encoded in typed Core operands, so neither Core
 * nor machine emitters carry a second GNU source output-count shadow.
 * Output operands form one leading prefix; reject malformed independent
 * Core producers instead of guessing a count. */
static inline bool minic_core_asm_output_operand_count(
    const MinicCoreInstruction *instruction, size_t *output_count) {
    size_t i, count = 0U;
    bool seen_input = false;
    if (instruction == NULL || output_count == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT)
        return false;
    for (i = 0U; i < instruction->value.structured_inline_asm.operand_count; ++i) {
        bool output;
        switch (instruction->value.structured_inline_asm.operands[i].kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            output = true;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            output = false;
            break;
        default:
            return false;
        }
        if (output && seen_input) return false;
        if (output) ++count;
        else seen_input = true;
    }
    *output_count = count;
    return true;
}

typedef struct MinicCoreTerminator {
    MinicCoreTerminatorKind kind;
    MinicSourceSpan span;
    MinicCoreValueId return_value;
    MinicCoreObjectId return_object;
    MinicCoreBlockId branch_target;
    MinicCoreValueId indirect_target;
    struct {
        MinicCoreValueId condition;
        MinicCoreBlockId when_true;
        MinicCoreBlockId when_false;
    } conditional;
} MinicCoreTerminator;

typedef struct MinicCoreBlock {
    MinicCoreInstructionId *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    MinicCoreTerminator terminator;
    bool has_terminator;
    /* A block may be reached through an opaque/indirect transfer even if it
     * has no assembler-visible alias (for example a specialized function).
     * This is control flow, not a source-language clone or label flag. */
    bool indirect_entry;
    /* Optional exported assembler-local alias. Module data relocations use
     * this exact symbol, and only the canonical owner publishes it. A block
     * may be an indirect entry without exporting any symbol. */
    char *local_label_symbol;
} MinicCoreBlock;

typedef struct MinicCoreFunction {
    char *name;
    size_t name_length;
    /* The attached type arena is borrowed and must outlive this function and
     * all target consumers. */
    const MinicCoreTypeContext *type_context;
    /* The sole sealed definition signature: return, formals, ABI convention
       and variadic policy all live in one nominal Core FUNCTION type. */
    MinicCoreTypeId core_function_type_id;
    MinicCoreFixedRegisterBinding *fixed_register_bindings;
    size_t fixed_register_binding_count;
    size_t fixed_register_binding_capacity;
    MinicCoreGlobal *globals;
    size_t global_count;
    size_t global_capacity;
    MinicCoreFunctionSymbol *function_symbols;
    size_t function_symbol_count;
    size_t function_symbol_capacity;
    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreCallSignature *call_signatures;
    size_t call_signature_count;
    size_t call_signature_capacity;
    MinicCoreInlineAsm *inline_asms;
    size_t inline_asm_count;
    size_t inline_asm_capacity;
    MinicCoreCallArgument *call_arguments;
    size_t call_argument_count;
    size_t call_argument_capacity;
    MinicCoreObject *objects;
    size_t object_count;
    size_t object_capacity;
    MinicCoreValue *values;
    size_t value_count;
    size_t value_capacity;
    MinicCoreInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    MinicCoreBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    MinicCoreBlockId entry_block;
} MinicCoreFunction;

/* For independent Core producers that have already populated all snapshots.
 * Both paths reject invalid/incomplete type identities and object layouts. */
bool minic_core_function_attach_type_context(MinicCoreFunction *function,
                                             const MinicCoreTypeContext *context);
bool minic_core_function_verify_core_type_snapshot(const MinicCoreFunction *function);
const MinicCoreType *minic_core_function_type_get(const MinicCoreFunction *function,
                                                  MinicCoreTypeId id);
/* Execution-kind queries for sealed Core types; never infer C rank or ABI. */
bool minic_core_function_type_is_kind(const MinicCoreFunction *function,
                                     MinicCoreTypeId id, MinicCoreTypeKind kind);
bool minic_core_function_value_is_kind(const MinicCoreFunction *function,
                                      MinicCoreValueId id, MinicCoreTypeKind kind);
/* Fully resolved element extent for private objects; explicit object alignment
 * remains independent of the underlying element's natural ABI alignment. */
bool minic_core_function_object_layout(const MinicCoreFunction *function,
                                       MinicCoreObjectId object_id,
                                       size_t *size, size_t *alignment);

void minic_core_function_initialize(MinicCoreFunction *function);
void minic_core_function_destroy(MinicCoreFunction *function);
static inline const MinicCoreType *minic_core_function_signature_type(
    const MinicCoreFunction *function) {
    const MinicCoreType *type;
    if (function == NULL || function->type_context == NULL ||
        function->core_function_type_id == MINIC_CORE_TYPE_INVALID)
        return NULL;
    type = minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context,
                                    function->core_function_type_id));
    return type != NULL && type->kind == MINIC_CORE_TYPE_FUNCTION ? type : NULL;
}
/* A one-shot, language-independent producer API. It publishes a complete
 * FUNCTION identity, not parallel signature fields on CoreFunction. */
bool minic_core_function_define_signature(MinicCoreFunction *function,
    MinicCoreTypeContext *context, MinicCoreTypeId return_type,
    const MinicCoreTypeId *parameters, size_t parameter_count,
    MinicFunctionCallingConvention convention, bool is_variadic);
static inline MinicCoreTypeId minic_core_function_return_type(
    const MinicCoreFunction *function) {
    const MinicCoreType *type = minic_core_function_signature_type(function);
    return type == NULL ? MINIC_CORE_TYPE_INVALID : type->value.function.return_type;
}
static inline const MinicCoreTypeId *minic_core_function_parameter_types(
    const MinicCoreFunction *function) {
    const MinicCoreType *type = minic_core_function_signature_type(function);
    return type == NULL ? NULL : type->value.function.parameters;
}
static inline size_t minic_core_function_parameter_count(
    const MinicCoreFunction *function) {
    const MinicCoreType *type = minic_core_function_signature_type(function);
    return type == NULL ? 0U : type->value.function.parameter_count;
}
static inline MinicFunctionCallingConvention minic_core_function_calling_convention(
    const MinicCoreFunction *function) {
    const MinicCoreType *type = minic_core_function_signature_type(function);
    return type == NULL ? (MinicFunctionCallingConvention)-1 :
                          type->value.function.calling_convention;
}
static inline bool minic_core_function_is_variadic(const MinicCoreFunction *function) {
    const MinicCoreType *type = minic_core_function_signature_type(function);
    return type != NULL && type->value.function.is_variadic;
}
bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id);
bool minic_core_function_add_function_symbol(MinicCoreFunction *function,
                                             const char *name,
                                             size_t name_length,
                                             MinicCoreFunctionSymbolId *symbol_id);
bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id);
bool minic_core_function_add_inline_asm_register_clobber(
    MinicCoreFunction *function,
    MinicCoreInlineAsmId inline_asm_id,
    const char *name,
    size_t name_length);
bool minic_core_function_add_inline_asm_symbol_reference(
    MinicCoreFunction *function, MinicCoreInlineAsmId inline_asm_id,
    MinicCoreInlineAsmSymbolKind kind, const char *name,
    size_t name_length, int64_t byte_addend);
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreCallArgument *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin);
bool minic_core_function_append_value_instruction(MinicCoreFunction *function,
                                                  MinicCoreBlockId block_id,
                                                  const MinicCoreInstruction *instruction,
                                                  MinicCoreValueId *value_id);
bool minic_core_function_append_effect_instruction(MinicCoreFunction *function,
                                                   MinicCoreBlockId block_id,
                                                   const MinicCoreInstruction *instruction);
bool minic_core_function_set_terminator(MinicCoreFunction *function,
                                        MinicCoreBlockId block_id,
                                        const MinicCoreTerminator *terminator);
/* Post-lowering semantic canonicalization. A branch consuming a literal Core
 * INTEGER_CONSTANT has exactly one possible successor; preserving the other
 * arm would create executable references to impossible GNU compile-time
 * assertions. This changes only CFG edges, never instruction side effects. */
size_t minic_core_function_fold_literal_branches(MinicCoreFunction *function);
/* Conservative CFG reachability shared by every target. Explicit local
 * exports and indirect entries are distinct roots; live ASM-goto and block
 * address producers contribute their own CFG successors. */
bool minic_core_function_mark_reachable_blocks(const MinicCoreFunction *function,
                                               bool *reachable, size_t capacity);
/* Machine-independent indirect landing-pad census. A reachable BLOCK_ADDRESS
 * makes its destination an indirect entry even for an independent Core
 * producer that did not pre-mark the target block. */
bool minic_core_function_mark_indirect_entries(const MinicCoreFunction *function,
                                               const bool *reachable,
                                               bool *indirect_entries, size_t capacity);
bool minic_core_function_verify(const MinicCoreFunction *function);
bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function);

#endif

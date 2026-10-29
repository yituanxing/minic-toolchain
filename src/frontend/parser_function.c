#include "frontend/attribute.h"
#include "frontend/declaration_sema.h"
#include "frontend/parser.h"
#include "frontend/parser_internal.h"
#include "frontend/parser_external_object.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool function_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

typedef struct MinicFunctionAttributeContext {
    bool allow_gnu_inline;
    bool allow_header_vector_pcs;
    bool is_internal;
    bool is_inline;
    bool is_extern;
    MinicFunctionCallingConvention *calling_convention;
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    bool *is_weak;
    MinicSymbolVisibility *visibility;
    bool *has_visibility;
    bool *is_noreturn;
    size_t *explicit_alignment;
    bool *force_emit;
    bool *is_constructor;
    MinicSourceSpan *alias_symbol_literal;
    const char *unsupported_message;
} MinicFunctionAttributeContext;

/* AArch64 glibc's math-vector.h has vector PCS declarations with opaque
 * NEON/SVE return types. Permit only those foreign declarations. Core
 * lowering still rejects calls or definitions involving opaque vectors. */
static bool aarch64_header_vector_return(MinicType type) {
    return type.pointer_depth == 0U &&
           (type.base_kind == MINIC_TYPE_BASE_AARCH64_FLOAT32X4 ||
            type.base_kind == MINIC_TYPE_BASE_AARCH64_FLOAT64X2 ||
            type.base_kind == MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32 ||
            type.base_kind == MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64 ||
            type.base_kind == MINIC_TYPE_BASE_AARCH64_SVE_BOOL);
}

static bool glibc_header_vector_pcs_attribute(const MinicParser *parser,
                                               const MinicParsedAttribute *attr) {
    return parser != NULL && attr != NULL && !attr->has_arguments &&
           minic_target_info_header_attribute_supported(
               parser->target_info,
               parser->source + attr->name_span.begin.offset,
               minic_parser_span_length(attr->name_span));
}

static bool function_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool function_attribute_list_has_kind(const MinicParsedAttributeList *attributes, MinicAttributeKind kind) {
    size_t index;

    if (attributes == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        const MinicAttributeDescriptor *descriptor = attributes->values[index].descriptor;
        if (descriptor != NULL && descriptor->kind == kind) {
            return true;
        }
    }
    return false;
}

static bool function_attribute_list_has_used(const MinicParsedAttributeList *attributes) {
    return function_attribute_list_has_kind(attributes, MINIC_ATTRIBUTE_USED);
}

static bool function_attribute_argument_token(MinicParser *parser,
                                              const MinicParsedAttribute *attribute,
                                              MinicTokenKind expected_kind,
                                              MinicSourceSpan *span) {
    MinicParser probe;

    if (parser == NULL || attribute == NULL || span == NULL || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) || probe.current.kind != expected_kind) {
        return false;
    }
    *span = probe.current.span;
    if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset) {
        return false;
    }
    return true;
}

static bool resolve_function_copy_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute) {
    MinicSourceSpan target_span;
    MinicFunctionId target_id;

    if (!function_attribute_argument_token(
            parser, attribute, MINIC_TOKEN_IDENTIFIER, &target_span)) {
        minic_parser_error(parser, "GNU copy requires one function identifier");
        return false;
    }
    target_id = minic_parser_find_function(parser, target_span);
    if (target_id == MINIC_FUNCTION_INVALID ||
        minic_c0_program_function(parser->program, target_id) == NULL) {
        minic_parser_error(parser, "GNU copy requires a previously declared function");
        return false;
    }
    /* MiniC currently persists symbol/layout function attributes separately from
     * the optimization/diagnostic attributes that GCC copy propagates. GCC copy
     * explicitly excludes alias/visibility/weak, so validating source identity is
     * the complete semantic effect for the currently persisted copy-eligible set. */
    return true;
}

/* Parse the symbol spelling now; resolve only after the entire TU exists.
 * Linux syscall wrappers declare GNU aliases before __se_sys_* definitions.
 * A literal is just a symbol lookup key, never an implicit declaration. */
static bool resolve_function_alias_attribute(
    MinicParser *parser, const MinicParsedAttribute *attribute,
    MinicSourceSpan *symbol_literal) {
    MinicSourceSpan literal;
    const char *name;
    size_t length;
    if (symbol_literal == NULL ||
        !function_attribute_argument_token(
            parser, attribute, MINIC_TOKEN_STRING_LITERAL, &literal) ||
        literal.end.offset <= literal.begin.offset + 2U) {
        minic_parser_error(parser, "GNU alias requires one string literal target");
        return false;
    }
    name = parser->source + literal.begin.offset + 1U;
    length = literal.end.offset - literal.begin.offset - 2U;
    if (length == 0U || memchr(name, '\\', length) != NULL) {
        minic_parser_error(parser, "escaped or empty GNU alias targets are not supported");
        return false;
    }
    *symbol_literal = literal;
    return true;
}

static bool parser_alias_literal_equal(
    const MinicParser *parser, MinicSourceSpan left, MinicSourceSpan right) {
    size_t a, b;
    if (parser == NULL ||
        left.end.offset < left.begin.offset + 2U ||
        right.end.offset < right.begin.offset + 2U)
        return false;
    a = left.end.offset - left.begin.offset - 2U;
    b = right.end.offset - right.begin.offset - 2U;
    return a == b &&
           memcmp(parser->source + left.begin.offset + 1U,
                  parser->source + right.begin.offset + 1U, a) == 0;
}

static bool parser_append_pending_function_alias(
    MinicParser *parser, MinicFunctionId id, MinicSourceSpan literal) {
    size_t i;
    MinicParserPendingFunctionAlias *next;
    size_t capacity;
    if (parser == NULL || id == MINIC_FUNCTION_INVALID ||
        literal.end.offset <= literal.begin.offset + 2U)
        return false;
    for (i = 0U; i < parser->pending_function_alias_count; ++i) {
        const MinicParserPendingFunctionAlias *old =
            &parser->pending_function_aliases[i];
        if (old->alias_id == id)
            return parser_alias_literal_equal(parser, old->symbol_literal, literal);
    }
    if (parser->pending_function_alias_count == parser->pending_function_alias_capacity) {
        capacity = parser->pending_function_alias_capacity == 0U
            ? 8U : parser->pending_function_alias_capacity * 2U;
        if (capacity < parser->pending_function_alias_capacity ||
            capacity > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(parser->pending_function_aliases, capacity * sizeof(*next));
        if (next == NULL)
            return false;
        parser->pending_function_aliases = next;
        parser->pending_function_alias_capacity = capacity;
    }
    parser->pending_function_aliases[parser->pending_function_alias_count].alias_id = id;
    parser->pending_function_aliases[parser->pending_function_alias_count].symbol_literal = literal;
    ++parser->pending_function_alias_count;
    return true;
}

static bool parser_resolve_pending_function_aliases(MinicParser *parser) {
    size_t i;
    if (parser == NULL || parser->target_info == NULL || parser->program == NULL)
        return false;
    for (i = 0U; i < parser->pending_function_alias_count; ++i) {
        const MinicParserPendingFunctionAlias *pending =
            &parser->pending_function_aliases[i];
        const MinicFunction *alias =
            minic_c0_program_function(parser->program, pending->alias_id);
        MinicFunctionId target_id = MINIC_FUNCTION_INVALID;
        const MinicFunction *target;
        size_t k, length;
        const char *name;
        length = pending->symbol_literal.end.offset -
                 pending->symbol_literal.begin.offset - 2U;
        name = parser->source + pending->symbol_literal.begin.offset + 1U;
        for (k = 0U; k < parser->program->function_count; ++k) {
            const MinicFunction *candidate =
                minic_c0_program_function(parser->program, k);
            const char *symbol = minic_c0_function_symbol_name(candidate);
            if (symbol != NULL && strlen(symbol) == length &&
                memcmp(symbol, name, length) == 0) {
                /* Ambiguous machine symbol names must not resolve
                 * according to source declaration order. */
                if (target_id != MINIC_FUNCTION_INVALID && target_id != k) {
                    minic_parser_error(parser, "ambiguous GNU alias symbol target");
                    return false;
                }
                target_id = k;
            }
        }
        if (target_id == MINIC_FUNCTION_INVALID) {
            minic_parser_error(
                parser, "GNU alias target must be declared in this translation unit");
            return false;
        }
        target = minic_c0_program_function(parser->program, target_id);
        if (alias == NULL || target == NULL ||
            !minic_target_info_function_alias_abi_compatible(
                parser->target_info, parser->program, alias, target) ||
            !minic_c0_program_set_function_alias(
                parser->program, pending->alias_id, target_id)) {
            minic_parser_error(parser,
                "GNU function alias requires a defined same-TU target with matching signature");
            return false;
        }
    }
    return true;
}

static bool consume_function_attribute(MinicParser *parser,
                                       const MinicParsedAttribute *attribute,
                                       void *opaque_context) {
    MinicFunctionAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicFunctionAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL && context->allow_header_vector_pcs &&
        glibc_header_vector_pcs_attribute(parser, attribute))
        return true;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)) {
        minic_parser_error(parser, "%s (attribute=%.*s)",
                           context->unsupported_message,
                           (int)minic_parser_span_length(attribute->name_span),
                           parser->source + attribute->name_span.begin.offset);
        return false;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_MS_ABI) {
        if (context->calling_convention == NULL || attribute->has_arguments ||
            !minic_target_info_function_calling_convention_scalar_call_supported(
                parser->target_info, MINIC_CALLING_CONVENTION_MS_X64)) {
            minic_parser_error(parser,
                "GNU ms_abi function attribute requires x86-64 scalar ABI support");
            return false;
        }
        *context->calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_NOCF_CHECK ||
        descriptor->kind == MINIC_ATTRIBUTE_INDIRECT_BRANCH ||
        descriptor->kind == MINIC_ATTRIBUTE_TARGET_CPU) {
        MinicSourceSpan literal;
        const char *value;
        size_t length;
        static const char *const safe_scalar_targets[] = {
            "default", "bmi2", "popcnt", "lzcnt",
            /* Linux 6.6 Zstd BMI2_TARGET_ATTRIBUTE expands to these three
             * optional scalar x86 feature names in one GCC target string. */
            "lzcnt,bmi,bmi2"
        };
        size_t index;

        if (!minic_target_info_gnu_function_security_attribute_supported(
                parser->target_info,
                parser->source + attribute->name_span.begin.offset,
                minic_parser_span_length(attribute->name_span))) {
            minic_parser_error(parser,
                "GNU x86 function-security attribute requires x86_64 target support");
            return false;
        }
        if (descriptor->kind == MINIC_ATTRIBUTE_NOCF_CHECK) {
            /* Native GCC's nocf_check disables IBT ENDBR insertion. MiniC's
             * current x86 backend does not insert ENDBR, so no emission change
             * is needed here. Full-kernel IBT eligibility is separate. */
            if (attribute->has_arguments) {
                minic_parser_error(parser, "GNU nocf_check takes no arguments");
                return false;
            }
            return true;
        }
        if (!function_attribute_argument_token(
                parser, attribute, MINIC_TOKEN_STRING_LITERAL, &literal) ||
            minic_parser_span_length(literal) < 2U) {
            minic_parser_error(parser,
                "GNU x86 function-security policy requires one string literal");
            return false;
        }
        value = parser->source + literal.begin.offset + 1U;
        length = minic_parser_span_length(literal) - 2U;
        if (descriptor->kind == MINIC_ATTRIBUTE_INDIRECT_BRANCH) {
            /* GCC's `keep` preserves ordinary indirect branches for marked
             * functions. MiniC presently emits those ordinary branches and
             * has no thunk/retpoline generator to select per declaration. */
            if (length == 4U && memcmp(value, "keep", 4U) == 0)
                return true;
            minic_parser_error(parser,
                "GNU indirect_branch policy requires supported value 'keep'");
            return false;
        }
        /* MiniC emits baseline x86 scalar integer instructions for these
         * optional scalar extensions; target("bmi2") does not demand BMI2
         * instructions or a different call ABI. Do not admit no-sse /
         * general-regs-only: those would constrain our existing FP backend.
         * Vector, ABI-changing and unrecognized strings fail closed. */
        for (index = 0U;
             index < sizeof(safe_scalar_targets) / sizeof(safe_scalar_targets[0]);
             ++index) {
            if (length == strlen(safe_scalar_targets[index]) &&
                memcmp(value, safe_scalar_targets[index], length) == 0)
                return true;
        }
        minic_parser_error(parser,
            "GNU x86 target attribute requests an unsupported machine policy");
        return false;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        if (context->explicit_alignment == NULL ||
            !minic_parser_apply_alignment_attribute(
                parser, attribute, "function", context->explicit_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0')
                minic_parser_error(parser, "GNU function alignment requires a power-of-two constant");
            return false;
        }
        if (*context->explicit_alignment > 1048576U) {
            minic_parser_error(parser, "GNU function alignment exceeds supported ELF symbol bound");
            return false;
        }
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_NO_SANITIZE) {
        MinicSourceSpan argument;
        /* Exact GCC/Clang UBSan control strings, including the forms used
         * by upstream Zstandard. MiniC emits no sanitizer instrumentation;
         * the subset is explicitly recognized, not silently discarded. */
        static const char *const names[] = {
            "\"undefined\"",
            "\"pointer-overflow\"",
            "\"signed-integer-overflow\""
        };
        size_t i;
        size_t length;

        if (!function_attribute_argument_token(
                parser, attribute, MINIC_TOKEN_STRING_LITERAL, &argument)) {
            minic_parser_error(parser, "GNU no_sanitize requires one string literal");
            return false;
        }
        length = minic_parser_span_length(argument);
        for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (length == strlen(names[i]) &&
                memcmp(parser->source + argument.begin.offset,
                       names[i], length) == 0) {
                return true;
            }
        }
        minic_parser_error(parser, "GNU no_sanitize unsupported sanitizer policy");
        return false;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_VISIBILITY) {
        MinicSourceSpan literal;
        const char *value;
        size_t len;
        MinicSymbolVisibility selected;

        if (context->visibility == NULL || context->has_visibility == NULL ||
            !function_attribute_argument_token(
                parser, attribute, MINIC_TOKEN_STRING_LITERAL, &literal) ||
            minic_parser_span_length(literal) < 2U) {
            minic_parser_error(parser, "GNU function visibility requires one string literal");
            return false;
        }
        value = parser->source + literal.begin.offset + 1U;
        len = minic_parser_span_length(literal) - 2U;
        if (len == 7U && memcmp(value, "default", 7U) == 0) {
            selected = MINIC_SYMBOL_VISIBILITY_DEFAULT;
        } else if (len == 6U && memcmp(value, "hidden", 6U) == 0) {
            selected = MINIC_SYMBOL_VISIBILITY_HIDDEN;
        } else if (len == 9U && memcmp(value, "protected", 9U) == 0) {
            selected = MINIC_SYMBOL_VISIBILITY_PROTECTED;
        } else if (len == 8U && memcmp(value, "internal", 8U) == 0) {
            selected = MINIC_SYMBOL_VISIBILITY_INTERNAL;
        } else {
            minic_parser_error(parser, "unsupported GNU function visibility value");
            return false;
        }
        if (context->is_internal) {
            minic_parser_error(parser, "GNU function visibility requires external linkage");
            return false;
        }
        if (*context->has_visibility && *context->visibility != selected) {
            minic_parser_error(parser, "conflicting GNU function visibility");
            return false;
        }
        *context->visibility = selected;
        *context->has_visibility = true;
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_COPY) {
        return resolve_function_copy_attribute(parser, attribute);
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_ALIAS) {
        MinicSourceSpan symbol_literal;
        if (context->alias_symbol_literal == NULL ||
            !resolve_function_alias_attribute(parser, attribute, &symbol_literal)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0')
                minic_parser_error(parser, "GNU alias requires a function declaration entity");
            return false;
        }
        if (context->alias_symbol_literal->end.offset != 0U &&
            !parser_alias_literal_equal(
                parser, *context->alias_symbol_literal, symbol_literal)) {
            minic_parser_error(parser, "conflicting GNU function alias attributes");
            return false;
        }
        *context->alias_symbol_literal = symbol_literal;
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
        if (context->section_name == NULL || context->section_name_length == NULL ||
            context->has_section == NULL || context->section_capacity == 0U ||
            !minic_parser_apply_section_attribute(parser,
                                                  attribute,
                                                  context->section_name,
                                                  context->section_capacity,
                                                  context->section_name_length,
                                                  context->has_section)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "%s", context->unsupported_message);
            }
            return false;
        }
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_WEAK) {
        if (context->is_weak == NULL || context->is_internal) {
            minic_parser_error(parser, "GNU weak requires external function linkage");
            return false;
        }
        *context->is_weak = true;
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_NORETURN) {
        if (context->is_noreturn != NULL) {
            *context->is_noreturn = true;
        }
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_USED) {
        if (context->force_emit != NULL) {
            *context->force_emit = true;
        }
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_CONSTRUCTOR) {
        /* Startup behavior is semantic, not a parse-only diagnostic. */
        if (context->is_constructor == NULL || attribute->has_arguments) {
            minic_parser_error(parser, "GNU constructor requires supported no-priority function");
            return false;
        }
        *context->is_constructor = true;
        return true;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {
        if (!context->allow_gnu_inline) {
            minic_parser_error(parser, "%s", context->unsupported_message);
            return false;
        }
        /* GNU extern inline keeps external symbol ownership with libc:
         * parse/validate the body, then retain only the declaration for
         * Core machine emission to avoid a duplicate global definition. */
        if (!context->is_inline) {
            minic_parser_error(parser, "GNU gnu_inline requires an inline declaration");
            return false;
        }
        return true;
    }

    /* returns_twice on an external setjmp declaration is admitted only
     * through the known control-flow descriptor. O0 callers retain a
     * concrete ABI frame across both normal and longjmp returns; a
     * GNU/QEMU setjmp/longjmp differential guards this contract. */
    if (!function_attribute_class_is_parse_only(descriptor->semantic_class)) {
        minic_parser_error(parser, "%s (attribute=%.*s)",
                           context->unsupported_message,
                           (int)minic_parser_span_length(attribute->name_span),
                           parser->source + attribute->name_span.begin.offset);
        return false;
    }
    return true;
}

static bool parse_function_attribute_lists(MinicParser *parser,
                                           bool allow_gnu_inline,
                                           bool is_internal,
                                           bool is_inline,
                                           bool *is_weak,
                                           const char *unsupported_message) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = allow_gnu_inline;
    context.allow_header_vector_pcs = false;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.is_extern = false;
    context.calling_convention = NULL;
    context.section_name = NULL;
    context.section_capacity = 0U;
    context.section_name_length = NULL;
    context.has_section = NULL;
    context.is_weak = is_weak;
    context.visibility = NULL;
    context.has_visibility = NULL;
    context.is_noreturn = NULL;
    context.explicit_alignment = NULL;
    context.force_emit = NULL;
    context.is_constructor = NULL;
    context.alias_symbol_literal = NULL;
    context.unsupported_message = unsupported_message;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
}

static bool apply_function_attribute_list(MinicParser *parser,
                                          const MinicParsedAttributeList *attributes,
                                          bool allow_gnu_inline,
                                          bool is_internal,
                                          bool is_inline,
                                          bool is_extern,
                                          bool allow_header_vector_pcs,
                                          MinicFunctionCallingConvention *calling_convention,
                                          char *section_name,
                                          size_t section_capacity,
                                          size_t *section_name_length,
                                          bool *has_section,
                                          bool *is_weak,
                                          MinicSymbolVisibility *visibility,
                                          bool *has_visibility,
                                          bool *is_noreturn,
                                          size_t *explicit_alignment,
                                          bool *is_constructor,
                                          MinicSourceSpan *alias_symbol_literal,
                                          const char *unsupported_message) {
    MinicFunctionAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    context.allow_gnu_inline = allow_gnu_inline;
    context.allow_header_vector_pcs = allow_header_vector_pcs;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.is_extern = is_extern;
    context.calling_convention = calling_convention;
    context.section_name = section_name;
    context.section_capacity = section_capacity;
    context.section_name_length = section_name_length;
    context.has_section = has_section;
    context.is_weak = is_weak;
    context.visibility = visibility;
    context.has_visibility = has_visibility;
    context.is_noreturn = is_noreturn;
    context.explicit_alignment = explicit_alignment;
    context.force_emit = NULL;
    context.is_constructor = is_constructor;
    context.alias_symbol_literal = alias_symbol_literal;
    context.unsupported_message = unsupported_message;
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_function_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

static bool section_attribute_token_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind == MINIC_TOKEN_EOF) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

bool minic_parser_parse_gnu_section_attribute(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_section) {
    for (;;) {
        MinicParser probe;
        char parsed[256];
        size_t parsed_length;

        if (parser == NULL || buffer == NULL || length == NULL || has_section == NULL ||
            capacity == 0U) {
            return false;
        }
        if (!section_attribute_token_is(parser, "__attribute__")) {
            return true;
        }

        probe = *parser;
        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe)) {
            return false;
        }
        if (!section_attribute_token_is(&probe, "section") &&
            !section_attribute_token_is(&probe, "__section__")) {
            return true;
        }

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__") ||
            (!section_attribute_token_is(parser, "section") &&
             !section_attribute_token_is(parser, "__section__")) ||
            !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after section")) {
            return false;
        }

        parsed_length = 0U;
        if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
            minic_parser_error(parser, "GNU section attribute requires a string literal");
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            size_t cursor;
            size_t end;

            if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
                minic_parser_error(parser, "invalid GNU section string");
                return false;
            }
            cursor = parser->current.span.begin.offset + 1U;
            end = parser->current.span.end.offset - 1U;
            while (cursor < end) {
                if (parser->source[cursor] == '\\') {
                    minic_parser_error(parser, "escaped GNU section names are not supported yet");
                    return false;
                }
                if (parsed_length + 1U >= sizeof(parsed)) {
                    minic_parser_error(parser, "GNU section name is too long");
                    return false;
                }
                parsed[parsed_length++] = parser->source[cursor++];
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
        if (parsed_length == 0U || parsed_length + 1U > capacity ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after section name") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU section attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU section attribute")) {
            return false;
        }
        parsed[parsed_length] = '\0';
        if (*has_section) {
            if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
                minic_parser_error(parser, "conflicting GNU section attributes");
                return false;
            }
        } else {
            (void)memcpy(buffer, parsed, parsed_length + 1U);
            *length = parsed_length;
            *has_section = true;
        }
    }
}

bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {
    return parse_function_attribute_lists(
        parser,
        false,
        false,
        false,
        NULL,
        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "
        "implemented explicitly");
}

bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                       bool is_internal,
                                                       bool is_inline) {
    return parse_function_attribute_lists(
        parser,
        true,
        is_internal,
        is_inline,
        NULL,
        "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be "
        "implemented explicitly");
}

static bool parse_persistent_function_attributes(MinicParser *parser,
                                                 bool is_internal,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 bool *is_weak,
                                                 bool *is_noreturn,
                                                 size_t *explicit_alignment,
                                                 bool *force_emit,
                                                 bool *is_constructor,
                                                 MinicSourceSpan *alias_symbol_literal,
                                                 MinicFunctionCallingConvention *calling_convention) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = false;
    context.allow_header_vector_pcs = false;
    context.is_internal = is_internal;
    context.is_inline = false;
    context.is_extern = false;
    context.calling_convention = calling_convention;
    context.section_name = section_name;
    context.section_capacity = section_capacity;
    context.section_name_length = section_name_length;
    context.has_section = has_section;
    context.is_weak = is_weak;
    context.visibility = NULL;
    context.has_visibility = NULL;
    context.is_noreturn = is_noreturn;
    context.explicit_alignment = explicit_alignment;
    context.force_emit = force_emit;
    context.is_constructor = is_constructor;
    context.alias_symbol_literal = alias_symbol_literal;
    context.unsupported_message =
        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "
        "implemented explicitly";
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
}

static bool parse_gnu_function_asm_label(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_label) {
    if (parser == NULL || buffer == NULL || length == NULL || has_label == NULL) {
        return false;
    }
    *length = 0U;
    *has_label = false;
    /* GNU accepts asm, __asm and __asm__ in declarations. Linux x86
     * current_stack_pointer uses asm("rsp"). Source grammar belongs here;
     * register admission is still a separate TargetInfo/backend decision. */
    if (!function_identifier_is(parser, "__asm__") &&
        !function_identifier_is(parser, "__asm") &&
        !function_identifier_is(parser, "asm")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __asm__")) {
        return false;
    }
    if (!minic_parser_parse_gnu_asm_name(parser, buffer, capacity, length))
        return false;
    *has_label = true;
    return minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm label");
}

static bool parse_gnu_visibility_name(MinicParser *parser, MinicSymbolVisibility *visibility) {
    MinicSourceSpan span;
    const char *value;
    size_t length;

    if (parser == NULL || visibility == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    span = parser->current.span;
    if (span.end.offset <= span.begin.offset + 1U) {
        minic_parser_error(parser, "invalid GNU visibility string");
        return false;
    }
    value = parser->source + span.begin.offset + 1U;
    length = span.end.offset - span.begin.offset - 2U;
    if (length == 8U && memcmp(value, "internal", 8U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
    } else if (length == 6U && memcmp(value, "hidden", 6U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    } else if (length == 9U && memcmp(value, "protected", 9U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
    } else if (length == 7U && memcmp(value, "default", 7U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        minic_parser_error(parser, "unsupported GNU visibility value");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_gnu_prefix_function_visibility(MinicParser *parser,
                                                 MinicSymbolVisibility *visibility,
                                                 bool *has_visibility) {
    if (parser == NULL || visibility == NULL || has_visibility == NULL) {
        return false;
    }
    /* Explicit prefix attributes and pragma default visibility have
       different ownership. This routine only parses explicit attributes;
       parse_function() folds the current pragma default in after storage
       class/linkage is known, so internal/static/register entities are not
       accidentally assigned symbol visibility. */
    *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    *has_visibility = false;
    while (function_identifier_is(parser, "__attribute__")) {
        MinicParser probe = *parser;

        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe)) {
            return false;
        }
        if (!function_identifier_is(&probe, "visibility")) {
            break;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }
        if (!function_identifier_is(parser, "visibility")) {
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after visibility") ||
            !parse_gnu_visibility_name(parser, visibility) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after visibility") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU attribute")) {
            return false;
        }
        *has_visibility = true;
    }
    return true;
}

typedef struct MinicParsedDeclarationPrefix {
    MinicParsedAttributeList attributes;
    bool is_extern;
    bool is_static;
    bool is_register;
    bool is_thread_local;
    bool is_inline;
    bool is_noreturn;
    /* GNU Linux interleaves const/volatile and declaration-head attributes.
     * Preserve both qualifiers and route each attribute to its real entity. */
    bool has_const_qualifier;
    bool has_volatile_qualifier;
} MinicParsedDeclarationPrefix;

static bool parse_declaration_prefix(MinicParser *parser,
                                     bool require_initial_static,
                                     MinicParsedDeclarationPrefix *prefix) {
    bool saw_storage_class;

    if (parser == NULL || prefix == NULL) {
        return false;
    }
    (void)memset(prefix, 0, sizeof(*prefix));
    saw_storage_class = false;

    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_KW_CONST ||
            parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
            if (parser->current.kind == MINIC_TOKEN_KW_CONST)
                prefix->has_const_qualifier = true;
            else
                prefix->has_volatile_qualifier = true;
            if (!minic_parser_advance(parser)) return false;
            continue;
        }
        if (function_identifier_is(parser, "register")) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_register = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "__thread") ||
            function_identifier_is(parser, "_Thread_local")) {
            if (prefix->is_thread_local) {
                minic_parser_error(parser, "duplicate thread-local storage-class specifier");
                return false;
            }
            prefix->is_thread_local = true;
            if (!minic_parser_advance(parser)) return false;
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_static = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_extern = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
            if (prefix->is_inline) {
                minic_parser_error(parser, "duplicate inline declaration specifier");
                return false;
            }
            prefix->is_inline = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "_Noreturn")) {
            if (prefix->is_noreturn) {
                minic_parser_error(parser, "duplicate _Noreturn declaration specifier");
                return false;
            }
            prefix->is_noreturn = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "__attribute__")) {
            size_t old_offset = parser->current.span.begin.offset;

            if (!minic_parser_collect_gnu_attribute_lists(parser, &prefix->attributes)) {
                return false;
            }
            if (parser->current.span.begin.offset == old_offset) {
                minic_parser_error(parser, "internal error: GNU attribute prefix made no progress");
                return false;
            }
            continue;
        }
        break;
    }

    if (require_initial_static && !prefix->is_static) {
        minic_parser_error(parser, "expected keyword 'static'");
        return false;
    }
    return true;
}

static bool parse_post_type_declaration_specifiers(
    MinicParser *parser, MinicParsedDeclarationPrefix *prefix) {
    bool saw_storage_class;

    if (parser == NULL || prefix == NULL) {
        return false;
    }
    saw_storage_class = prefix->is_extern || prefix->is_static || prefix->is_register;

    for (;;) {
        if (function_identifier_is(parser, "register")) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_register = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "__thread") ||
            function_identifier_is(parser, "_Thread_local")) {
            if (prefix->is_thread_local) {
                minic_parser_error(parser, "duplicate thread-local storage-class specifier");
                return false;
            }
            prefix->is_thread_local = true;
            if (!minic_parser_advance(parser)) return false;
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_static = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_extern = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
            if (prefix->is_inline) {
                minic_parser_error(parser, "duplicate inline declaration specifier");
                return false;
            }
            prefix->is_inline = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "_Noreturn")) {
            if (prefix->is_noreturn) {
                minic_parser_error(parser, "duplicate _Noreturn declaration specifier");
                return false;
            }
            prefix->is_noreturn = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

static bool record_function_declaration_entity(MinicParser *parser,
                                               MinicSourceSpan name_span,
                                               MinicType return_type,
                                               const MinicType *parameter_types,
                                               size_t parameter_count,
                                               bool is_variadic,
                                               MinicFunctionCallingConvention calling_convention,
                                               bool is_internal,
                                               bool is_noreturn,
                                               bool is_weak,
                                               const char *assembler_name,
                                               size_t assembler_name_length,
                                               bool has_assembler_name,
                                               MinicSymbolVisibility visibility,
                                               bool has_visibility,
                                               const char *section_name,
                                               size_t section_name_length,
                                               bool has_section,
                                               size_t explicit_alignment) {
    MinicFunctionId function_id;
    const MinicFunction *existing_function;

    if (parser == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL)) {
        return false;
    }
    if (is_weak && is_internal) {
        minic_parser_error(parser, "GNU weak requires external function linkage");
        return false;
    }
    if (calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
        !minic_parser_calling_convention_fixed_scalar_supported(
            parser, calling_convention, return_type, parameter_types,
            parameter_count, is_variadic)) {
        minic_parser_error(parser,
            "ms_abi function declaration requires supported fixed scalar signature");
        return false;
    }
    function_id = minic_parser_find_function(parser, name_span);
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                parser->program, existing_function, return_type,
                parameter_types, parameter_count, is_variadic) ||
            (!existing_function->is_internal && is_internal)) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
        if (existing_function->is_internal) {
            is_internal = true;
        }
        if (calling_convention == MINIC_CALLING_CONVENTION_DEFAULT)
            calling_convention = existing_function->calling_convention;
        else if (existing_function->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
                 existing_function->calling_convention != calling_convention) {
            minic_parser_error(parser, "conflicting function calling convention");
            return false;
        }
    } else {
        if (minic_parser_find_global_object_entity(parser, name_span) !=
                MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_function(parser->program,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           parser->program->local_count,
                                           0U,
                                           MINIC_BLOCK_INVALID,
                                           &function_id) ||
            !minic_c0_program_set_function_signature(
                parser->program, function_id, return_type, parameter_types, parameter_count) ||
            !minic_c0_program_set_function_internal(parser->program, function_id, is_internal) ||
            !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare function entity");
            }
            return false;
        }
    }
    if (!minic_c0_program_set_function_calling_convention(
            parser->program, function_id, calling_convention)) {
        minic_parser_error(parser, "conflicting function calling convention");
        return false;
    }
    if (is_noreturn &&
        !minic_c0_program_set_function_noreturn(parser->program, function_id, true)) {
        minic_parser_error(parser, "cannot persist noreturn function metadata");
        return false;
    }
    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {
        minic_parser_error(parser, "conflicting GNU weak function linkage");
        return false;
    }
    if (has_assembler_name &&
        !minic_c0_program_set_function_assembler_name(
            parser->program, function_id, assembler_name, assembler_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function asm label");
        return false;
    }
    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    if (has_section && !minic_c0_program_set_function_section(
                           parser->program, function_id, section_name, section_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function section");
        return false;
    }
    if (explicit_alignment != 0U &&
        !minic_c0_program_set_function_alignment(parser->program, function_id,
                                                 explicit_alignment)) {
        minic_parser_error(parser, "invalid GNU function entry alignment");
        return false;
    }
    return true;
}

static bool finish_function_declaration_entity(MinicParser *parser,
                                               MinicSourceSpan name_span,
                                               MinicType return_type,
                                               const MinicType *parameter_types,
                                               size_t parameter_count,
                                               bool is_variadic,
                                               MinicFunctionCallingConvention calling_convention,
                                               bool is_internal,
                                               bool is_noreturn,
                                               bool is_weak,
                                               const char *assembler_name,
                                               size_t assembler_name_length,
                                               bool has_assembler_name,
                                               MinicSymbolVisibility visibility,
                                               bool has_visibility,
                                               const char *section_name,
                                               size_t section_name_length,
                                               bool has_section,
                                               size_t explicit_alignment,
                                               bool force_emit,
                                               MinicSourceSpan alias_symbol_literal) {
    MinicFunctionId function_id;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        !record_function_declaration_entity(parser,
                                            name_span,
                                            return_type,
                                            parameter_types,
                                            parameter_count,
                                            is_variadic,
                                            calling_convention,
                                            is_internal,
                                            is_noreturn,
                                            is_weak,
                                            assembler_name,
                                            assembler_name_length,
                                            has_assembler_name,
                                            visibility,
                                            has_visibility,
                                            section_name,
                                            section_name_length,
                                            has_section,
                                            explicit_alignment)) {
        return false;
    }
    function_id = minic_parser_find_function(parser, name_span);
    if (function_id == MINIC_FUNCTION_INVALID) {
        return false;
    }
    if (force_emit &&
        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {
        minic_parser_error(parser, "cannot persist GNU used function metadata");
        return false;
    }
    if (alias_symbol_literal.end.offset != 0U) {
        if (has_section ||
            !parser_append_pending_function_alias(
                parser, function_id, alias_symbol_literal)) {
            minic_parser_error(
                parser, "conflicting or invalid GNU function alias declaration");
            return false;
        }
    }
    return minic_parser_advance(parser);
}

static bool parse_function(MinicParser *parser, bool is_internal) {
    MinicSourceSpan name_span;
    MinicSourceSpan parameter_name_spans[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType base_type;
    MinicType return_type;
    MinicParsedAttributeList deferred_attributes;
    MinicParsedAttributeList declarator_attributes;
    MinicParsedDeclarationPrefix declaration_prefix;
    MinicBlockId body_block;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    MinicLocal parameter_local;
    MinicLocalId parameter_local_id;
    size_t parameter_count;
    size_t local_begin;
    size_t local_count;
    bool is_extern_declaration;
    bool is_function_pointer_object;
    bool has_preparsed_function_parameters;
    bool is_inline;
    bool is_gnu_extern_inline;
    bool is_noreturn;
    bool is_constructor;
    bool force_emit;
    bool is_register_declaration;
    bool is_static_declaration;
    bool is_main;
    bool is_variadic;
    MinicFunctionCallingConvention calling_convention;
    bool is_weak;
    char assembler_name[256];
    size_t assembler_name_length;
    bool has_assembler_name;
    char section_name[256];
    size_t section_name_length;
    bool has_section;
    size_t object_explicit_alignment;
    MinicSymbolVisibility visibility;
    bool has_visibility;
    bool object_is_weak;
    MinicSourceSpan alias_symbol_literal;

    body_block = MINIC_BLOCK_INVALID;
    parameter_count = 0U;
    is_extern_declaration = false;
    is_function_pointer_object = false;
    has_preparsed_function_parameters = false;
    is_inline = false;
    is_gnu_extern_inline = false;
    is_noreturn = false;
    is_constructor = false;
    force_emit = false;
    is_register_declaration = false;
    is_static_declaration = false;
    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
    (void)memset(&declarator_attributes, 0, sizeof(declarator_attributes));
    (void)memset(&declaration_prefix, 0, sizeof(declaration_prefix));
    is_variadic = false;
    calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    is_weak = false;
    assembler_name_length = 0U;
    has_assembler_name = false;
    section_name_length = 0U;
    has_section = false;
    object_explicit_alignment = 0U;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    has_visibility = false;
    object_is_weak = false;
    (void)memset(&alias_symbol_literal, 0, sizeof(alias_symbol_literal));
    (void)memset(assembler_name, 0, sizeof(assembler_name));
    (void)memset(section_name, 0, sizeof(section_name));
    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));
    (void)memset(parameter_types, 0, sizeof(parameter_types));
    if (!parse_gnu_prefix_function_visibility(parser, &visibility, &has_visibility)) {
        return false;
    }
    if (!parse_declaration_prefix(parser, is_internal, &declaration_prefix)) {
        return false;
    }
    deferred_attributes = declaration_prefix.attributes;
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        (declaration_prefix.has_const_qualifier &&
         !minic_type_add_const(base_type, &base_type)) ||
        (declaration_prefix.has_volatile_qualifier &&
         !minic_type_add_volatile(base_type, &base_type)) ||
        !parse_post_type_declaration_specifiers(parser, &declaration_prefix) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
    /* The front end accepts opaque extern TLS declarations found in
     * original target libc headers, but cannot emit a TLS definition or
     * pretend TLS memory access is ordinary GOT addressing. */
    if (declaration_prefix.is_thread_local && !declaration_prefix.is_extern) {
        minic_parser_error(parser, "thread-local storage definitions are not yet supported");
        return false;
    }
    is_extern_declaration = declaration_prefix.is_extern;
    is_static_declaration = declaration_prefix.is_static;
    is_register_declaration = declaration_prefix.is_register;
    is_internal = declaration_prefix.is_static;
    is_inline = declaration_prefix.is_inline;
    is_noreturn = declaration_prefix.is_noreturn;
    if (!has_visibility && !is_internal && !is_register_declaration &&
        parser->default_visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT) {
        visibility = parser->default_visibility;
        has_visibility = true;
    }
    return_type = base_type;
    for (;;) {
        if (!minic_parser_parse_pointer_declarator(parser, return_type, &return_type) ||
            !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
            return false;
        }
        /* GNU permits declarator attributes between pointer levels, for example
           `char * __attribute__((unused)) *fn(void)`.  Keep those attributes in
           the existing deferred entity-routing list, then resume the same pointer
           declarator rather than mistaking the following `*` for a missing name. */
        if (parser->current.kind != MINIC_TOKEN_STAR) {
            break;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser name_probe = *parser;

        if (!minic_parser_advance(&name_probe)) {
            return false;
        }
        if (name_probe.current.kind == MINIC_TOKEN_STAR) {
            MinicParsedFunctionDeclarator declarator;

            if (!minic_parser_parse_parenthesized_function_declarator(
                    parser, true, true, &declarator)) {
                return false;
            }
            if (!minic_parser_build_function_declarator_type(
                    parser, return_type, &declarator, &return_type)) {
                minic_parser_error(parser, "cannot build parenthesized function declarator type");
                return false;
            }
            name_span = declarator.name_span;
            declarator_attributes = declarator.attributes;
            if (declarator.has_inner_function_suffix) {
                size_t inner_parameter_index;

                if (declarator.inner_parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
                    minic_parser_error(parser, "inner function parameter count exceeds compiler limit");
                    return false;
                }
                parameter_count = declarator.inner_parameter_count;
                is_variadic = declarator.inner_is_variadic;
                for (inner_parameter_index = 0U;
                     inner_parameter_index < parameter_count;
                     ++inner_parameter_index) {
                    parameter_types[inner_parameter_index] =
                        declarator.inner_parameter_types[inner_parameter_index];
                    parameter_name_spans[inner_parameter_index] =
                        declarator.inner_parameter_name_spans[inner_parameter_index];
                }
                has_preparsed_function_parameters = true;
                is_function_pointer_object = false;
            } else {
                if (declarator.is_variadic) {
                    minic_parser_error(parser,
                                       "variadic function pointer objects are not supported yet");
                    return false;
                }
                is_function_pointer_object = true;
            }
        } else {
            bool name_is_array;

            name_is_array = false;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected function or object name in parenthesized declarator");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
                MinicType array_type;

                if (!minic_parser_parse_array_declarator_suffix(
                        parser, return_type, true, &array_type, &name_is_array) ||
                    !name_is_array) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser,
                                           "cannot build parenthesized array declarator type");
                    }
                    return false;
                }
                return_type = array_type;
            }
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized declarator")) {
                return false;
            }
        }
    } else {
        minic_parser_error(parser, "expected function or extern object name");
        return false;
    }

    force_emit = function_attribute_list_has_used(&deferred_attributes) ||
                 function_attribute_list_has_used(&declarator_attributes);

    if (is_register_declaration) {
        MinicFixedRegisterBindingId binding_id;

        if (is_inline || deferred_attributes.count != 0U || has_section || has_visibility ||
            is_function_pointer_object || has_preparsed_function_parameters ||
            parser->current.kind == MINIC_TOKEN_LPAREN ||
            (!minic_type_is_integer(return_type) && !minic_type_is_pointer(return_type))) {
            minic_parser_error(parser, "unsupported file-scope register declaration shape");
            return false;
        }
        if (!parse_gnu_function_asm_label(parser,
                                          assembler_name,
                                          sizeof(assembler_name),
                                          &assembler_name_length,
                                          &has_assembler_name) ||
            !has_assembler_name) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "file-scope register declaration requires GNU asm name");
            }
            return false;
        }
        if (!minic_target_info_fixed_register_supported(
                parser->target_info, assembler_name, assembler_name_length)) {
            minic_parser_error(parser, "fixed register binding is not supported by this target");
            return false;
        }
        if (!minic_c0_program_add_fixed_register_binding(parser->program,
                                                         parser->source + name_span.begin.offset,
                                                         minic_parser_span_length(name_span),
                                                         return_type,
                                                         assembler_name,
                                                         assembler_name_length,
                                                         &binding_id)) {
            minic_parser_error(parser, "cannot record fixed register binding");
            return false;
        }
        (void)binding_id;
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after fixed register binding");
    }
    if (!is_function_pointer_object && minic_type_is_function(return_type)) {
        const MinicFunctionType *function_type;
        MinicType typed_return_type;
        MinicType typed_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t typed_parameter_count;
        size_t parameter_index;
        bool typed_is_variadic;
        MinicFunctionCallingConvention typed_calling_convention;
        bool declaration_is_weak;

        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            minic_parser_error(parser,
                               "function-typed declarator cannot add another function suffix");
            return false;
        }
        function_type =
            minic_c0_program_function_type(parser->program, return_type.function_type_id);
        if (function_type == NULL ||
            function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "invalid function-typed declarator signature");
            return false;
        }
        /* FunctionType is Program-owned growable storage. Snapshot its canonical
         * signature before any subsequent semantic operation can grow owner pools. */
        typed_return_type = function_type->return_type;
        typed_parameter_count = function_type->parameter_count;
        typed_is_variadic = function_type->is_variadic;
        typed_calling_convention = function_type->calling_convention;
        typed_is_variadic = function_type->is_variadic;
        for (parameter_index = 0U; parameter_index < typed_parameter_count; ++parameter_index) {
            typed_parameter_types[parameter_index] =
                function_type->parameter_types[parameter_index];
        }
        if (!apply_function_attribute_list(
                parser,
                &deferred_attributes,
                true,
                is_internal,
                is_inline,
                is_extern_declaration,
                !is_internal && aarch64_header_vector_return(typed_return_type),
                &typed_calling_convention,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &is_weak,
                &visibility,
                &has_visibility,
                &is_noreturn,
                &object_explicit_alignment,
                NULL, /* function typed declarator is not startup hook */
                NULL,
                "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes "
                "must be implemented explicitly")) {
            return false;
        }
        declaration_is_weak = is_weak;

        for (;;) {
            bool entity_is_weak;
            bool entity_is_noreturn;
            bool entity_force_emit;
            size_t entity_explicit_alignment = object_explicit_alignment;

            if (parser->current.kind == MINIC_TOKEN_LPAREN) {
                minic_parser_error(parser,
                                   "function-typed declarator cannot add another function suffix");
                return false;
            }
            entity_is_weak = declaration_is_weak;
            entity_is_noreturn = is_noreturn;
            entity_force_emit = force_emit;
            assembler_name_length = 0U;
            has_assembler_name = false;
            (void)memset(assembler_name, 0, sizeof(assembler_name));
            if (!parse_gnu_function_asm_label(parser,
                                              assembler_name,
                                              sizeof(assembler_name),
                                              &assembler_name_length,
                                              &has_assembler_name) ||
                !parse_persistent_function_attributes(
                    parser,
                    is_internal,
                    NULL,
                    0U,
                    NULL,
                    NULL,
                    &entity_is_weak,
                    &entity_is_noreturn,
                    &entity_explicit_alignment,
                    &entity_force_emit,
                    NULL,
                    NULL,
                    &typed_calling_convention)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA &&
                parser->current.kind != MINIC_TOKEN_SEMICOLON) {
                minic_parser_error(
                    parser, "function-typed declarators currently support declarations only");
                return false;
            }
            if (!record_function_declaration_entity(parser,
                                                    name_span,
                                                    typed_return_type,
                                                    typed_parameter_types,
                                                    typed_parameter_count,
                                                    typed_is_variadic,
                                                    typed_calling_convention,
                                                    is_internal,
                                                    entity_is_noreturn,
                                                    entity_is_weak,
                                                    assembler_name,
                                                    assembler_name_length,
                                                    has_assembler_name,
                                                    visibility,
                                                    has_visibility,
                                                    section_name,
                                                    section_name_length,
                                                    has_section,
                                                    entity_explicit_alignment)) {
                return false;
            }
            function_id = minic_parser_find_function(parser, name_span);
            if (function_id == MINIC_FUNCTION_INVALID ||
                (entity_force_emit &&
                 !minic_c0_program_set_function_force_emit(
                     parser->program, function_id, true))) {
                minic_parser_error(parser, "cannot persist GNU used function metadata");
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
                return minic_parser_advance(parser);
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_direct_declarator_name(parser, &name_span)) {
                return false;
            }
        }
    }
    if (is_static_declaration && !has_preparsed_function_parameters &&
        parser->current.kind != MINIC_TOKEN_LPAREN) {
        MinicGlobalObjectId object_id;

        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (is_noreturn) {
            minic_parser_error(parser, "_Noreturn specifier requires a function declarator");
            return false;
        }
        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment) ||
            !minic_parser_apply_object_attribute_list(parser,
                                                      &declarator_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (has_visibility) {
            minic_parser_error(parser,
                               "static object symbol attributes require explicit object semantics");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_static_zero_declaration_list_after_head(
                parser,
                base_type,
                return_type,
                name_span,
                section_name,
                section_name_length,
                has_section,
                object_explicit_alignment);
        }
        if (!minic_parser_parse_static_global_after_head(parser,
                                                         base_type,
                                                         return_type,
                                                         name_span,
                                                         section_name,
                                                         sizeof(section_name),
                                                         &section_name_length,
                                                         &has_section,
                                                         &object_explicit_alignment)) {
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_parser_external_object_metadata(parser,
                                            object_id,
                                            section_name,
                                            section_name_length,
                                            has_section,
                                            object_explicit_alignment,
                                            MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                            false)) {
            minic_parser_error(parser, "cannot persist static object metadata");
            return false;
        }
        return true;
    }
    if (!is_internal && !has_preparsed_function_parameters &&
        (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (is_noreturn) {
            minic_parser_error(parser, "_Noreturn specifier requires a function declarator");
            return false;
        }
        if (!minic_parser_apply_object_attribute_list_with_symbol_metadata(
                parser,
                &deferred_attributes,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &object_is_weak) ||
            !minic_parser_apply_object_attribute_list_with_symbol_metadata(
                parser,
                &declarator_attributes,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &object_is_weak)) {
            return false;
        }
        if (is_extern_declaration) {
            MinicGlobalObjectId existing_id =
                minic_parser_find_global_object_entity(parser, name_span);
            const MinicGlobalObject *existing =
                minic_c0_program_global_object(parser->program, existing_id);
            if (existing != NULL &&
                existing->is_thread_local != declaration_prefix.is_thread_local) {
                minic_parser_error(parser, "conflicting TLS qualification on extern object");
                return false;
            }
            if (declaration_prefix.is_thread_local &&
                parser->current.kind != MINIC_TOKEN_SEMICOLON) {
                minic_parser_error(parser, "GNU extern TLS supports declarations only");
                return false;
            }
            if (!minic_parser_parse_extern_global_after_head(parser,
                                                             base_type,
                                                             return_type,
                                                             name_span,
                                                             section_name,
                                                             section_name_length,
                                                             has_section,
                                                             object_explicit_alignment,
                                                             visibility,
                                                             has_visibility) ||
                !minic_parser_external_object_weak(parser, name_span, object_is_weak)) {
                return false;
            }
            if (declaration_prefix.is_thread_local) {
                MinicGlobalObjectId id =
                    minic_parser_find_global_object_entity(parser, name_span);
                if (id == MINIC_GLOBAL_OBJECT_INVALID ||
                    id >= parser->program->global_object_count)
                    return false;
                parser->program->global_objects[id].is_thread_local = true;
            }
            return true;
        }
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(
                parser,
                section_name,
                sizeof(section_name),
                &section_name_length,
                &has_section,
                &object_explicit_alignment,
                &visibility,
                &has_visibility,
                &object_is_weak)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (object_is_weak) {
                minic_parser_error(parser, "GNU weak declaration lists are not supported yet");
                return false;
            }
            return minic_parser_external_tentative_declaration_list_after_head(parser,
                                                                        base_type,
                                                                        return_type,
                                                                        name_span,
                                                                        section_name,
                                                                        section_name_length,
                                                                        has_section,
                                                                        object_explicit_alignment,
                                                                        visibility,
                                                                        has_visibility);
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_external_visible_array(parser,
                                              return_type,
                                              name_span,
                                              section_name,
                                              sizeof(section_name),
                                              &section_name_length,
                                              &has_section,
                                              &object_explicit_alignment,
                                              visibility,
                                              has_visibility,
                                              &object_is_weak)) {
                return false;
            }
        } else if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            if (!minic_parser_external_tentative_object(parser,
                                                 return_type,
                                                 name_span,
                                                 section_name,
                                                 section_name_length,
                                                 has_section,
                                                 object_explicit_alignment,
                                                 visibility,
                                                 has_visibility)) {
                return false;
            }
        } else if (!minic_parser_external_object_definition(parser,
                                                     return_type,
                                                     name_span,
                                                     section_name,
                                                     section_name_length,
                                                     has_section,
                                                     object_explicit_alignment,
                                                     visibility,
                                                     has_visibility)) {
            return false;
        }
        return minic_parser_external_object_weak(parser, name_span, object_is_weak);
    }
    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            is_extern_declaration,
            !is_internal && aarch64_header_vector_return(return_type),
            &calling_convention,
            section_name,
            sizeof(section_name),
            &section_name_length,
            &has_section,
            &is_weak,
            &visibility,
            &has_visibility,
            &is_noreturn,
            &object_explicit_alignment,
            &is_constructor,
            &alias_symbol_literal,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly")) {
        return false;
    }

    is_gnu_extern_inline = is_inline && is_extern_declaration &&
        (function_attribute_list_has_kind(
            &deferred_attributes, MINIC_ATTRIBUTE_GNU_INLINE) ||
         function_attribute_list_has_kind(
            &declarator_attributes, MINIC_ATTRIBUTE_GNU_INLINE));
    function_id = minic_parser_find_function(parser, name_span);
    is_main = minic_parser_span_length(name_span) == 4U &&
              memcmp(parser->source + name_span.begin.offset, "main", 4U) == 0;
    if (is_main && !minic_type_is_integer(return_type)) {
        minic_parser_error(parser, "main must return int");
        return false;
    }
    if (is_main && is_internal) {
        minic_parser_error(parser, "main cannot have internal linkage");
        return false;
    }

    if (!has_preparsed_function_parameters &&
        (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
         !minic_parser_parse_parameter_list(
             parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
         !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'"))) {
        return false;
    }
    if (!parse_gnu_function_asm_label(parser,
                                      assembler_name,
                                      sizeof(assembler_name),
                                      &assembler_name_length,
                                      &has_assembler_name) ||
        !parse_persistent_function_attributes(parser,
                                              is_internal,
                                              section_name,
                                              sizeof(section_name),
                                              &section_name_length,
                                              &has_section,
                                              &is_weak,
                                              &is_noreturn,
                                              &object_explicit_alignment,
                                              &force_emit,
                                              &is_constructor,
                                              &alias_symbol_literal,
                                              &calling_convention)) {
        return false;
    }
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                parser->program, existing_function, return_type,
                parameter_types, parameter_count, is_variadic) ||
            (!existing_function->is_internal && is_internal)) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
        if (existing_function->is_internal) {
            is_internal = true;
        }
        /* An unannotated redeclaration inherits a previously selected
         * ABI. An explicit incompatible ABI must never silently replace it. */
        if (calling_convention == MINIC_CALLING_CONVENTION_DEFAULT)
            calling_convention = existing_function->calling_convention;
        else if (existing_function->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
                 existing_function->calling_convention != calling_convention) {
            minic_parser_error(parser, "conflicting function calling convention");
            return false;
        }
    }

    if (is_constructor && (!minic_type_is_void(return_type) ||
                           parameter_count != 0U || is_variadic ||
                           parser->current.kind == MINIC_TOKEN_SEMICOLON)) {
        minic_parser_error(parser, "GNU constructor requires a defined void(void) function");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return finish_function_declaration_entity(parser,
                                                  name_span,
                                                  return_type,
                                                  parameter_types,
                                                  parameter_count,
                                                  is_variadic,
                                                  calling_convention,
                                                  is_internal,
                                                  is_noreturn,
                                                  is_weak,
                                                  assembler_name,
                                                  assembler_name_length,
                                                  has_assembler_name,
                                                  visibility,
                                                  has_visibility,
                                                  section_name,
                                                  section_name_length,
                                                  has_section,
                                                  object_explicit_alignment,
                                                  force_emit,
                                                  alias_symbol_literal);
    }
    /* An MS x64 definition is a distinct incoming ABI, not a SysV body
     * with merely an attribute attached. The x86 target implements fixed
     * scalar ingress and all MS nonvolatile preservation, while variable
     * arguments, aggregates, TI and x87 remain explicitly unsupported. */
    if (calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
        !minic_parser_calling_convention_fixed_scalar_supported(
            parser, calling_convention, return_type, parameter_types,
            parameter_count, is_variadic)) {
        minic_parser_error(parser,
            "ms_abi function definition requires supported fixed scalar signature");
        return false;
    }
    if (calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
        calling_convention != MINIC_CALLING_CONVENTION_MS_X64) {
        minic_parser_error(parser,
            "unsupported nondefault function definition calling convention");
        return false;
    }
    if (alias_symbol_literal.end.offset != 0U) {
        minic_parser_error(parser, "GNU alias applies to declarations, not function definitions");
        return false;
    }
    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&
        !minic_type_is_pointer(return_type) && !minic_type_is_float(return_type) &&
        !minic_type_is_double(return_type) &&
        !minic_type_is_long_double(return_type) &&
        !minic_type_is_record(return_type)) {
        minic_parser_error(parser, "unsupported function return type");
        return false;
    }
    if ((minic_type_is_record(return_type) || minic_type_is_enum(return_type)) &&
        !minic_parser_require_complete_object_type(
            parser, return_type, "function definition requires a complete return type")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected ';' or '{' after function declarator");
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            minic_c0_local_initialize(&parameter_local);
            if ((minic_type_is_record(parameter_types[parameter_index]) ||
                 minic_type_is_enum(parameter_types[parameter_index])) &&
                !minic_parser_require_complete_object_type(
                    parser,
                    parameter_types[parameter_index],
                    "function definition requires complete object parameter types")) {
                return false;
            }
        }
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            if (minic_parser_span_length(parameter_name_spans[parameter_index]) == 0U) {
                minic_parser_error(parser, "function definition requires parameter names");
                return false;
            }
        }
    }
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (existing_function == NULL || existing_function->is_defined) {
            minic_parser_error(parser, "duplicate function definition");
            return false;
        }
    }

    if (!minic_parser_advance(parser) ||
        !minic_c0_program_add_block(parser->program, &body_block)) {
        if (body_block == MINIC_BLOCK_INVALID) {
            minic_parser_error(parser, "out of memory while adding function body");
        }
        return false;
    }

    local_begin = parser->program->local_count;
    parser->local_begin = local_begin;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            (void)memset(&parameter_local, 0, sizeof(parameter_local));
            parameter_local.name_span = parameter_name_spans[parameter_index];
            parameter_local.type = parameter_types[parameter_index];
            parameter_local.element_count = 1U;
            parameter_local.explicit_alignment = 0U;
            parameter_local.is_array = false;
            parameter_local.is_register_storage = false;
            if (minic_parser_find_local_in_current_scope(parser, parameter_local.name_span) !=
                MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "duplicate parameter name");
                return false;
            }
            if (!minic_c0_program_add_local(
                    parser->program, &parameter_local, &parameter_local_id)) {
                minic_parser_error(parser, "out of memory while adding parameter");
                return false;
            }
            if (!minic_parser_bind_local(parser, parameter_local.name_span, parameter_local_id)) {
                return false;
            }
        }
    }

    if (function_id == MINIC_FUNCTION_INVALID) {
        if (!minic_c0_program_add_function(parser->program,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           local_begin,
                                           parameter_count,
                                           body_block,
                                           &function_id) ||
            !minic_c0_program_set_function_signature(
                parser->program, function_id, return_type, parameter_types, parameter_count) ||
            !minic_c0_program_set_function_internal(parser->program, function_id, is_internal) ||
            !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
            minic_parser_error(parser, "out of memory while adding function");
            return false;
        }
    } else if (!minic_c0_program_define_function(
                   parser->program, function_id, local_begin, body_block)) {
        minic_parser_error(parser, "cannot define previously declared function");
        return false;
    }
    /* The definition path must persist the same ABI identity as a preceding
     * prototype: otherwise Core silently defaults the body to SysV even
     * though callers correctly use Microsoft's RCX/RDX/R8/R9 convention. */
    if (!minic_c0_program_set_function_calling_convention(
            parser->program, function_id, calling_convention)) {
        minic_parser_error(parser, "conflicting function definition calling convention");
        return false;
    }
    if (!minic_c0_program_set_function_inline(parser->program, function_id, is_inline)) {
        minic_parser_error(parser, "cannot persist inline function metadata");
        return false;
    }
    if (force_emit &&
        !minic_c0_program_set_function_force_emit(parser->program, function_id, true)) {
        minic_parser_error(parser, "cannot persist GNU used function metadata");
        return false;
    }
    if (is_noreturn &&
        !minic_c0_program_set_function_noreturn(parser->program, function_id, true)) {
        minic_parser_error(parser, "cannot persist noreturn function metadata");
        return false;
    }
    if (is_constructor &&
        !minic_c0_program_set_function_constructor(parser->program, function_id)) {
        minic_parser_error(parser, "cannot persist GNU constructor startup metadata");
        return false;
    }
    if (is_weak && !minic_c0_program_set_function_weak(parser->program, function_id, true)) {
        minic_parser_error(parser, "conflicting GNU weak function linkage");
        return false;
    }
    if (has_assembler_name &&
        !minic_c0_program_set_function_assembler_name(
            parser->program, function_id, assembler_name, assembler_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function asm label");
        return false;
    }
    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    if (has_section && !minic_c0_program_set_function_section(
                           parser->program, function_id, section_name, section_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function section");
        return false;
    }
    if (object_explicit_alignment != 0U &&
        !minic_c0_program_set_function_alignment(parser->program, function_id,
                                                 object_explicit_alignment)) {
        minic_parser_error(parser, "invalid GNU function entry alignment");
        return false;
    }
    parser->current_function = function_id;
    parser->current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    if (is_main) {
        parser->program->entry_function = function_id;
        parser->program->body_block = body_block;
    }

    parser->current_block = body_block;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            return false;
        }
        if (!minic_parser_parse_statement(parser, true)) {
            return false;
        }
    }
    {
        size_t statement_index;

        for (statement_index = parser->function_statement_begin;
             statement_index < parser->program->statement_count;
             ++statement_index) {
            const MinicStatement *statement;

            statement = minic_c0_program_statement(parser->program, statement_index);
            if (statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&
                statement->target_statement == statement_index) {
                minic_parser_error(parser, "address of unknown label");
                return false;
            }
        }
    }
    if (!minic_parser_materialize_cleanup_contexts(parser, MINIC_CLEANUP_CONTEXT_ROOT) ||
        ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
          !minic_type_is_record(return_type) && !minic_parser_add_default_return(parser)) ||
         !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'"))) {
        return false;
    }

    local_count = parser->program->local_count - local_begin;
    if (!minic_c0_program_finish_function(parser->program, function_id, local_count)) {
        minic_parser_error(parser, "invalid local range while finishing function");
        return false;
    }
    if (is_gnu_extern_inline) {
        /* GNU extern inline provides a parse-validated inline-only body,
         * not a strong definition: the existing external libc symbol
         * remains the sole concrete implementation. */
        MinicFunction *fn = &parser->program->functions[function_id];
        fn->is_defined = false;
        fn->body_block = MINIC_BLOCK_INVALID;
        fn->local_count = 0U;
    }
    minic_parser_end_scope(parser);
    parser->current_function = MINIC_FUNCTION_INVALID;
    parser->current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    return true;
}

static bool top_level_is_gnu_asm(const MinicParser *parser) {
    return function_identifier_is(parser, "asm") || function_identifier_is(parser, "__asm") ||
           function_identifier_is(parser, "__asm__");
}

static bool parse_top_level_gnu_basic_asm(MinicParser *parser) {
    char *assembly_text;
    size_t assembly_length;
    MinicSourceSpan assembly_span;

    if (parser == NULL || !top_level_is_gnu_asm(parser)) {
        return false;
    }
    assembly_text = NULL;
    assembly_length = 0U;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        minic_parser_error(parser, "file-scope GNU basic asm does not allow qualifiers");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_string_text(parser, &assembly_text, &assembly_length, &assembly_span)) {
        free(assembly_text);
        return false;
    }
    (void)assembly_span;
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        free(assembly_text);
        minic_parser_error(parser, "file-scope GNU basic asm does not support operands");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after file-scope GNU asm") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after file-scope GNU asm") ||
        !minic_c0_program_add_file_asm(parser->program, assembly_text, assembly_length)) {
        free(assembly_text);
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot store file-scope GNU basic asm");
        }
        return false;
    }
    free(assembly_text);
    return true;
}

static bool top_level_is_gnu_extension_marker(const MinicParser *parser) {
    static const char marker[] = "__extension__";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(marker) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, marker, length) == 0;
}

static bool skip_top_level_gnu_extension_markers(MinicParser *parser) {
    while (top_level_is_gnu_extension_marker(parser)) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool enum_keyword_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;

    if (parser == NULL || is_standalone == NULL || parser->current.kind != MINIC_TOKEN_KW_ENUM) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected enum tag or definition after enum keyword");
        return false;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_standalone =
        probe.current.kind == MINIC_TOKEN_SEMICOLON || probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

static bool record_keyword_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;
    size_t token_length;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind != MINIC_TOKEN_LBRACE) {
        if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record tag or definition after record keyword");
            return false;
        }

        token_length = minic_parser_span_length(probe.current.span);
        if (token_length == 13U &&
            memcmp(parser->source + probe.current.span.begin.offset, "__attribute__", 13U) == 0) {
            *is_standalone = true;
            return true;
        }

        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_LBRACE) {
        *is_standalone = false;
        return true;
    }
    /* A record definition can itself declare objects:
     *     struct switch_t { ... } *cur_switch;
     * Peek after the matching '}' without mutating the real parser,
     * even when record fields contain nested anonymous records. */
    {
        size_t brace_depth = 0U;
        do {
            if (probe.current.kind == MINIC_TOKEN_EOF) {
                minic_parser_error(parser, "unterminated record definition");
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_LBRACE) {
                ++brace_depth;
            } else if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                --brace_depth;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        } while (brace_depth != 0U);
    }
    /* GNU C allows type attributes after a complete record definition,
     * e.g. struct grep_globals { ... } __attribute__((may_alias));
     * Look through those attribute lists before distinguishing a standalone
     * type declaration from a declaration with an object declarator.
     * The actual parser will validate and apply all type attributes. */
    {
        MinicParsedAttributeList type_attributes;
        (void)memset(&type_attributes, 0, sizeof(type_attributes));
        if (!minic_parser_collect_gnu_attribute_lists(&probe, &type_attributes)) {
            return false;
        }
    }
    *is_standalone = probe.current.kind == MINIC_TOKEN_SEMICOLON;
    return true;
}

static bool finalize_tentative_incomplete_arrays(MinicC0Program *program) {
    size_t index;

    if (program == NULL) {
        return false;
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        object = minic_c0_program_global_object(program, index);
        if (object == NULL || !object->is_tentative || !minic_type_is_array(object->type)) {
            continue;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        if (array_type->element_count != 0U || array_type->is_zero_length) {
            continue;
        }
        if (array_type->is_query_materialized ||
            !minic_c0_program_complete_array_type(program, object->type, 1U)) {
            return false;
        }
    }
    return true;
}

bool minic_parse_c0_program_for_target(const char *path,
                                       const char *source,
                                       size_t length,
                                       const MinicTargetInfo *target_info,
                                       MinicC0Program *program,
                                       MinicDiagnostic *diagnostic) {
    MinicParser parser;
    bool success;

    (void)memset(&parser, 0, sizeof(parser));
    parser.cache_owner = &parser;
    parser.path = path;
    parser.source = source;
    parser.diagnostic = diagnostic;
    parser.program = program;
    parser.target_info = target_info;
    if (target_info == NULL) {
        return false;
    }
    parser.default_visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    parser.visibility_depth = 0U;
    parser.current_block = MINIC_BLOCK_INVALID;
    parser.current_function = MINIC_FUNCTION_INVALID;
    parser.current_function_name_object = MINIC_GLOBAL_OBJECT_INVALID;
    parser.continue_target_statement = MINIC_STATEMENT_INVALID;
    minic_lexer_initialize(&parser.lexer, path, source, length);

    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        success = skip_top_level_gnu_extension_markers(&parser);
        if (!success || parser.current.kind == MINIC_TOKEN_EOF) {
            break;
        }
        if (parser.current.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
            success = minic_parser_parse_preprocessor_directive(&parser);
        } else if (top_level_is_gnu_asm(&parser)) {
            success = parse_top_level_gnu_basic_asm(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {
            success = minic_parser_advance(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {
            success = minic_parser_parse_static_assert_declaration(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {
            success = minic_parser_parse_typedef(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            success = parse_function(&parser, false);
        } else if (parser.current.kind == MINIC_TOKEN_KW_INLINE) {
            success = parse_function(&parser, false);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            success = parse_function(&parser, true);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STRUCT ||
                   parser.current.kind == MINIC_TOKEN_KW_UNION) {
            bool is_standalone;

            if (!record_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_record_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {
            bool is_standalone;

            if (!enum_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_enum_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
        } else {
            success = parse_function(&parser, false);
        }
    }
    if (success && !parser_resolve_pending_function_aliases(&parser))
        success = false;
    if (success && !finalize_tentative_incomplete_arrays(program)) {
        minic_parser_error(&parser, "cannot complete tentative incomplete array definition");
        success = false;
    }
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0') {
        minic_parser_error(&parser, "parser failed without diagnostic");
    }
    if (getenv("MINIC_PARSER_PERF_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_PARSER_LOOKUPS local=%zu/%zu function=%zu/%zu "
                      "global=%zu/%zu typedef=%zu/%zu enum=%zu/%zu "
                      "fixed=%zu/%zu builtin_mem=%zu/%zu "
                      "functions=%zu globals=%zu aliases=%zu expressions=%zu\n",
                      parser.perf_find_local_calls, parser.perf_find_local_steps,
                      parser.perf_find_function_calls, parser.perf_find_function_steps,
                      parser.perf_find_global_calls, parser.perf_find_global_steps,
                      parser.perf_find_type_alias_calls, parser.perf_find_type_alias_steps,
                      parser.perf_find_enum_calls, parser.perf_find_enum_steps,
                      parser.perf_find_fixed_calls, parser.perf_find_fixed_steps,
                      parser.perf_builtin_mem_calls, parser.perf_builtin_mem_steps,
                      program->function_count, program->global_object_count,
                      program->type_alias_count, program->expression_count);
    }
    free(parser.perf_function_name_slots);
    parser.perf_function_name_slots = NULL;
    parser.perf_function_name_slot_capacity = 0U;
    parser.perf_function_name_indexed_count = 0U;
    free(parser.perf_global_name_slots);
    parser.perf_global_name_slots = NULL;
    parser.perf_global_name_slot_capacity = 0U;
    parser.perf_global_name_indexed_count = 0U;
    free(parser.perf_enum_name_slots);
    parser.perf_enum_name_slots = NULL;
    parser.perf_enum_name_slot_capacity = 0U;
    parser.perf_enum_name_indexed_count = 0U;
    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    free(parser.switch_contexts);
    parser.switch_contexts = NULL;
    free(parser.pending_function_aliases);
    parser.pending_function_aliases = NULL;
    return success;
}

/* Retain the historical parser API for RV64-focused tests and tools. */
bool minic_parse_c0_program(const char *path,
                            const char *source,
                            size_t length,
                            MinicC0Program *program,
                            MinicDiagnostic *diagnostic) {
    return minic_parse_c0_program_for_target(path, source, length,
                                              minic_default_target_info(),
                                              program, diagnostic);
}

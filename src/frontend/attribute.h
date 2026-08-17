#ifndef MINIC_FRONTEND_ATTRIBUTE_H
#define MINIC_FRONTEND_ATTRIBUTE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicAttributeKind {
    MINIC_ATTRIBUTE_INVALID = 0,
    MINIC_ATTRIBUTE_NOTHROW,
    MINIC_ATTRIBUTE_LEAF,
    MINIC_ATTRIBUTE_NONNULL,
    MINIC_ATTRIBUTE_ACCESS,
    MINIC_ATTRIBUTE_PURE,
    MINIC_ATTRIBUTE_CONST,
    MINIC_ATTRIBUTE_MALLOC,
    MINIC_ATTRIBUTE_ALLOC_SIZE,
    MINIC_ATTRIBUTE_ASSUME_ALIGNED,
    MINIC_ATTRIBUTE_UNUSED,
    MINIC_ATTRIBUTE_USED,
    MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
    MINIC_ATTRIBUTE_NO_PROFILE_INSTRUMENT_FUNCTION,
    MINIC_ATTRIBUTE_NO_SANITIZE_ADDRESS,
    MINIC_ATTRIBUTE_NO_STACK_PROTECTOR,
    MINIC_ATTRIBUTE_ALWAYS_INLINE,
    MINIC_ATTRIBUTE_NOINLINE,
    MINIC_ATTRIBUTE_NOCLONE,
    MINIC_ATTRIBUTE_EXTERNALLY_VISIBLE,
    MINIC_ATTRIBUTE_COLD,
    MINIC_ATTRIBUTE_NORETURN,
    MINIC_ATTRIBUTE_DEPRECATED,
    MINIC_ATTRIBUTE_ERROR,
    MINIC_ATTRIBUTE_WARNING,
    MINIC_ATTRIBUTE_FORMAT,
    MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,
    MINIC_ATTRIBUTE_GNU_INLINE,
    MINIC_ATTRIBUTE_COPY,
    MINIC_ATTRIBUTE_ALIAS,
    MINIC_ATTRIBUTE_WEAK,
    MINIC_ATTRIBUTE_SECTION,
    MINIC_ATTRIBUTE_VISIBILITY,
    MINIC_ATTRIBUTE_DESIGNATED_INIT,
    MINIC_ATTRIBUTE_PACKED,
    MINIC_ATTRIBUTE_ALIGNED,
    MINIC_ATTRIBUTE_TRANSPARENT_UNION,
    MINIC_ATTRIBUTE_CLEANUP,
    MINIC_ATTRIBUTE_FALLTHROUGH
} MinicAttributeKind;

typedef enum MinicAttributeClass {
    MINIC_ATTRIBUTE_CLASS_INFORMATIONAL = 0,
    MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
    MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
    MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,
    MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
    MINIC_ATTRIBUTE_CLASS_SYMBOL,
    MINIC_ATTRIBUTE_CLASS_LAYOUT
} MinicAttributeClass;

typedef enum MinicAttributeTarget {
    MINIC_ATTRIBUTE_TARGET_NONE = 0,
    MINIC_ATTRIBUTE_TARGET_FUNCTION = 1U << 0,
    MINIC_ATTRIBUTE_TARGET_OBJECT = 1U << 1,
    MINIC_ATTRIBUTE_TARGET_TYPE = 1U << 2,
    MINIC_ATTRIBUTE_TARGET_FIELD = 1U << 3,
    MINIC_ATTRIBUTE_TARGET_STATEMENT = 1U << 4
} MinicAttributeTarget;

typedef struct MinicAttributeDescriptor {
    const char *name;
    size_t name_length;
    MinicAttributeKind kind;
    MinicAttributeClass semantic_class;
    unsigned int allowed_targets;
    size_t minimum_argument_count;
    size_t maximum_argument_count;
    bool validates_argument_count;
} MinicAttributeDescriptor;

const MinicAttributeDescriptor *minic_attribute_lookup(const char *name, size_t name_length);
bool minic_attribute_allowed_on(const MinicAttributeDescriptor *descriptor,
                                MinicAttributeTarget target);
bool minic_attribute_argument_count_allowed(const MinicAttributeDescriptor *descriptor,
                                            size_t argument_count);

#endif

#include "frontend/attribute.h"

#include <string.h>

#define MINIC_ATTRIBUTE_ENTRY(name_value, kind_value, class_value, targets_value)                  \
    { name_value, sizeof(name_value) - 1U, kind_value, class_value, targets_value }

static const MinicAttributeDescriptor minic_attribute_descriptors[] = {
    MINIC_ATTRIBUTE_ENTRY("__nothrow__",
                          MINIC_ATTRIBUTE_NOTHROW,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__leaf__",
                          MINIC_ATTRIBUTE_LEAF,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__nonnull__",
                          MINIC_ATTRIBUTE_NONNULL,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("nonnull",
                          MINIC_ATTRIBUTE_NONNULL,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__access__",
                          MINIC_ATTRIBUTE_ACCESS,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__pure__",
                          MINIC_ATTRIBUTE_PURE,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("const",
                          MINIC_ATTRIBUTE_CONST,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__const__",
                          MINIC_ATTRIBUTE_CONST,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__malloc__",
                          MINIC_ATTRIBUTE_MALLOC,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__unused__",
                          MINIC_ATTRIBUTE_UNUSED,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__no_instrument_function__",
                          MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__always_inline__",
                          MINIC_ATTRIBUTE_ALWAYS_INLINE,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__cold__",
                          MINIC_ATTRIBUTE_COLD,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("cold",
                          MINIC_ATTRIBUTE_COLD,
                          MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("noreturn",
                          MINIC_ATTRIBUTE_NORETURN,
                          MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__noreturn__",
                          MINIC_ATTRIBUTE_NORETURN,
                          MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("deprecated",
                          MINIC_ATTRIBUTE_DEPRECATED,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__deprecated__",
                          MINIC_ATTRIBUTE_DEPRECATED,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT |
                              MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("error",
                          MINIC_ATTRIBUTE_ERROR,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__error__",
                          MINIC_ATTRIBUTE_ERROR,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("warning",
                          MINIC_ATTRIBUTE_WARNING,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__warning__",
                          MINIC_ATTRIBUTE_WARNING,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("format",
                          MINIC_ATTRIBUTE_FORMAT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__format__",
                          MINIC_ATTRIBUTE_FORMAT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("warn_unused_result",
                          MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__warn_unused_result__",
                          MINIC_ATTRIBUTE_WARN_UNUSED_RESULT,
                          MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("__gnu_inline__",
                          MINIC_ATTRIBUTE_GNU_INLINE,
                          MINIC_ATTRIBUTE_CLASS_LANGUAGE_SEMANTIC,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
    MINIC_ATTRIBUTE_ENTRY("section",
                          MINIC_ATTRIBUTE_SECTION,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
    MINIC_ATTRIBUTE_ENTRY("__section__",
                          MINIC_ATTRIBUTE_SECTION,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
    MINIC_ATTRIBUTE_ENTRY("visibility",
                          MINIC_ATTRIBUTE_VISIBILITY,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
    MINIC_ATTRIBUTE_ENTRY("__visibility__",
                          MINIC_ATTRIBUTE_VISIBILITY,
                          MINIC_ATTRIBUTE_CLASS_SYMBOL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION | MINIC_ATTRIBUTE_TARGET_OBJECT),
    MINIC_ATTRIBUTE_ENTRY("packed",
                          MINIC_ATTRIBUTE_PACKED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE),
    MINIC_ATTRIBUTE_ENTRY("__packed__",
                          MINIC_ATTRIBUTE_PACKED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE),
    MINIC_ATTRIBUTE_ENTRY("aligned",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
    MINIC_ATTRIBUTE_ENTRY("__aligned__",
                          MINIC_ATTRIBUTE_ALIGNED,
                          MINIC_ATTRIBUTE_CLASS_LAYOUT,
                          MINIC_ATTRIBUTE_TARGET_TYPE | MINIC_ATTRIBUTE_TARGET_FIELD),
};

const MinicAttributeDescriptor *minic_attribute_lookup(const char *name, size_t name_length) {
    size_t index;

    if (name == NULL || name_length == 0U) {
        return NULL;
    }
    for (index = 0U;
         index < sizeof(minic_attribute_descriptors) / sizeof(minic_attribute_descriptors[0]);
         ++index) {
        const MinicAttributeDescriptor *descriptor;

        descriptor = &minic_attribute_descriptors[index];
        if (descriptor->name_length == name_length &&
            memcmp(descriptor->name, name, name_length) == 0) {
            return descriptor;
        }
    }
    return NULL;
}

bool minic_attribute_allowed_on(const MinicAttributeDescriptor *descriptor,
                                MinicAttributeTarget target) {
    return descriptor != NULL && target != MINIC_ATTRIBUTE_TARGET_NONE &&
           (descriptor->allowed_targets & (unsigned int)target) != 0U;
}

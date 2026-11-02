#ifndef MINIC_COMMON_SOURCE_LOCATION_H
#define MINIC_COMMON_SOURCE_LOCATION_H

#include <stddef.h>

/* Source coordinates are diagnostic provenance, not C token semantics.
 * Both the language frontend and the language-neutral Core can carry them. */
typedef struct MinicSourcePosition {
    size_t offset;
    size_t line;
    size_t column;
} MinicSourcePosition;

typedef struct MinicSourceSpan {
    MinicSourcePosition begin;
    MinicSourcePosition end;
} MinicSourceSpan;

#endif

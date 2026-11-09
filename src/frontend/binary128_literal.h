#ifndef MINIC_FRONTEND_BINARY128_LITERAL_H
#define MINIC_FRONTEND_BINARY128_LITERAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse a decimal/hex C floating literal into the common IEEE binary128
 * Core representation. The selected target's 64- or 113-bit significand
 * precision is rounded *before* storage, avoiding x87 double rounding. */
bool minic_binary128_parse_literal(const char *text, size_t length,
                                   unsigned int precision,
                                   uint64_t *low, uint64_t *high);

#endif

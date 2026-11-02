#ifndef MINIAS_SOURCE_REPLAY_CONTRACT_H
#define MINIAS_SOURCE_REPLAY_CONTRACT_H
#include "minias_target_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Common owns this vector; SourceGraph only rebinds persistent site identity
 * to pass-local Fragment ordinals. The Fragment records are never copied. */
typedef struct MiniAsFragmentGrowthSet {
    size_t *ids;
    size_t count;
    size_t capacity;
} MiniAsFragmentGrowthSet;

/* Only these ISA-neutral parse-shape values are meaningful across complete
 * SourceGraph replays. Numeric-local and macro-generated symbol spellings
 * are intentionally excluded: the new Common parse rebinds their values. */
typedef struct MiniAsForcedOperandShape {
    MiniAsMCOperandKind kind;
    MiniAsMCRegisterClass reg_class;
    uint16_t target_reg_bank;
    unsigned reg;
    bool register_64;
} MiniAsForcedOperandShape;

/* A live, borrowed Common Fragment projected one ordinal at a time. Its
 * string is valid only during the caller's synchronous SourceGraph query.
 * Source cannot see a writable Common Fragment, retained MCInst, payload,
 * lifetime capacity, or Target-owned memory/relocation plan. */
typedef struct MiniAsSourceReplayFragment {
    MiniAsStableSiteId source_site;
    const char *op;
    size_t line;
    uint32_t size;
    bool instruction;
    unsigned opcode;
    uint32_t target_form;
    uint32_t mode_bits;
    unsigned operand_count;
} MiniAsSourceReplayFragment;

/* One pass-local, non-retained read capability into Common's *sole* live
 * Fragment graph. The callbacks may not publish a mutable pointer or retain
 * SourceGraph state. They are queried during source validation, branch
 * lookup/rebinding, and capture; growth_plan delegates to Common MCLayout's
 * existing typed Target relaxation plan, not another Target parser. */
typedef struct MiniAsSourceReplayReader {
    void *owner;
    size_t fragment_count;
    bool (*fragment)(void *owner,size_t ordinal,
                     MiniAsSourceReplayFragment *out);
    bool (*operand)(void *owner,size_t ordinal,unsigned operand_index,
                    MiniAsForcedOperandShape *out);
    bool (*growth_plan)(void *owner,size_t ordinal,
                        uint32_t *required_size,uint32_t *terminal_size);
} MiniAsSourceReplayReader;
#endif

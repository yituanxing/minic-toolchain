#ifndef MINIAS_SOURCE_REPLAY_CONTRACT_H
#define MINIAS_SOURCE_REPLAY_CONTRACT_H
#include <stddef.h>
/* Common owns this vector; SourceGraph only rebinds persistent site identity
 * to pass-local Fragment ordinals. The Fragment records are never copied. */
typedef struct MiniAsFragmentGrowthSet {
    size_t *ids;
    size_t count;
    size_t capacity;
} MiniAsFragmentGrowthSet;
#endif

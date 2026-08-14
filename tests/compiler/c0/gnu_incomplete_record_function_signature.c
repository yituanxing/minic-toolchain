struct Range;

/* Linux memory_hotplug.h shape: declaration may return an incomplete record. */
struct Range arch_get_mappable_range(void);
void consume_range(struct Range);
typedef struct Range (*range_transform_t)(struct Range);

struct SignatureHolder {
    range_transform_t transform;
};

struct Range {
    unsigned long start;
    unsigned long end;
};

/* Completion must preserve the same record identity in all earlier signatures. */
struct Range arch_get_mappable_range(void);
void consume_range(struct Range);

typedef struct Range (*range_transform_after_t)(struct Range);
_Static_assert(__builtin_types_compatible_p(range_transform_t, range_transform_after_t),
               "forward and completed record signatures must retain identity");

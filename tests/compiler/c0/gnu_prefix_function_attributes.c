static inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
    __attribute__((__no_instrument_function__)) __attribute__((__always_inline__)) const int *
prefix_attribute_identity(const int *value)
{
    return value;
}

const int *call_prefix_attribute_identity(const int *value)
{
    return prefix_attribute_identity(value);
}

inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
    __attribute__((__no_instrument_function__)) int prefix_external_identity(int value)
{
    return value;
}


/* Linux signal/start_kernel shapes: externally_visible preserves public reachability
 * under whole-program optimization. MiniC never internalizes public symbols, so the
 * bounded semantic effect is intentionally parse-only while external linkage stays. */
__attribute__((__externally_visible__))
void externally_visible_decl(int value);

__attribute__((__externally_visible__)) __attribute__((__cold__))
__attribute__((__section__(".probe.externally-visible.text")))
__attribute__((__no_sanitize_address__)) __attribute__((__no_stack_protector__))
void externally_visible_decl(int value)
{
    (void)value;
}

extern __attribute__((__externally_visible__)) int externally_visible_object;

int *externally_visible_object_address(void)
{
    return &externally_visible_object;
}


__attribute__((no_sanitize_address)) __attribute__((no_stack_protector))
void instrumentation_policy_aliases(void)
{
}

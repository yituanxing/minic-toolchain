/* Linux CONFIG-disabled static object/relocation graph: a dead object must
 * not keep an otherwise unreachable callback and its external callee alive.
 * The retained object provides the opposite GNU `used` contract. */
extern int minic_dead_static_object_must_not_resolve(void);

static int dead_callback(void)
{
    return minic_dead_static_object_must_not_resolve();
}

static int (*const dead_ops)(void) = dead_callback;

static int retained_callback(void)
{
    return 17;
}

static int (*const retained_ops)(void) __attribute__((used)) = retained_callback;

/* A non-exported object in a linker-consumed section may be externally
 * retained independently of an explicit C reference. */
static int retained_initcall __attribute__((used, section(".initcall6.init"))) = 23;

int minic_dead_static_object_live_entry(void)
{
    if (0)
        return dead_ops();
    return 41;
}

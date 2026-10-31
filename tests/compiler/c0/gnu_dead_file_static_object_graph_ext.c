/* Joint Core function/object DCE: positive roots deliberately combine
 * non-C linker/ASM reachability and ordinary runtime references. */
extern int minic_dead_graph_callback_must_not_resolve(void);

static int unused_callback(void)
{
    return minic_dead_graph_callback_must_not_resolve();
}

static int (*const unused_ops)(void) = unused_callback;

static int used_callback(void)
{
    return 17;
}

static int (*const used_ops)(void) __attribute__((used)) = used_callback;

/* Section roots can be read by GNU linker scripts even without C use. */
static int section_callback(void)
{
    return 19;
}

static int (*const section_ops)(void)
    __attribute__((section(".initcall6.init"))) = section_callback;

/* This TU-local variable is named only by opaque file-scope GNU ASM. */
static int asm_only_data = 23;
asm(".pushsection .minic.static.graph.asmroot,\"a\"\n"
    ".quad asm_only_data\n"
    ".popsection\n");

static volatile int runtime_flag = 1;
static int runtime_data = 29;

int minic_dead_graph_live_entry(void)
{
    if (0)
        return unused_ops();
    if (runtime_flag)
        return runtime_data;
    return 41;
}

/* Object aliases with external linkage cannot disappear with their targets. */
int minic_dead_graph_alias_storage = 31;
extern int minic_dead_graph_public_alias
    __attribute__((alias("minic_dead_graph_alias_storage")));

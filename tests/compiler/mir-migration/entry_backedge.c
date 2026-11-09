/* Forces a real CFG edge back into the function's first Core block; a
 * serializer must not mistake the symbol entry for an ordinary local label. */
volatile int mir_backedge_sink;

__attribute__((noinline))
int mir_backedge(int count) {
entry:
    if (count <= 0) return mir_backedge_sink;
    mir_backedge_sink += count;
    --count;
    goto entry;
}

int main(void) { return mir_backedge(4); }

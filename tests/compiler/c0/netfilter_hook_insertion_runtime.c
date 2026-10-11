/* Regression: dynamic Netfilter hook insertion must reload the local
   "inserted" flag after a loop backedge. The old MiniC core.o folded
   if (inserted || priority > old_ops[i]->priority) to an unconditional
   edge, producing an out-of-bounds hook array write under Linux 6.6.143. */
struct hook_op { unsigned int priority; unsigned int token; };
struct hook_entry { unsigned int token; unsigned int priv; };

static int check_insert(unsigned int priority, unsigned int expected_position) {
    struct hook_op old0 = {10U, 10U};
    struct hook_op old1 = {20U, 20U};
    struct hook_op old2 = {30U, 30U};
    struct hook_op new_op = {0U, 99U};
    const struct hook_op *old_ops[3];
    struct hook_entry old_hooks[3];
    struct hook_entry new_hooks[9];
    unsigned int old_entries = 3U;
    unsigned int i = 0U, nhooks = 0U, j;
    int inserted = 0;

    old_ops[0] = &old0; old_ops[1] = &old1; old_ops[2] = &old2;
    for (j = 0U; j < 3U; j++) {
        old_hooks[j].token = old_ops[j]->token;
        old_hooks[j].priv = 100U + j;
    }
    new_op.priority = priority;
    while (i < old_entries && nhooks < 9U) {
        if (inserted || new_op.priority > old_ops[i]->priority) {
            new_hooks[nhooks] = old_hooks[i];
            i++;
        } else {
            new_hooks[nhooks].token = new_op.token;
            new_hooks[nhooks].priv = 999U;
            inserted = 1;
        }
        nhooks++;
    }
    if (!inserted && nhooks < 9U) {
        new_hooks[nhooks].token = new_op.token;
        new_hooks[nhooks].priv = 999U;
        nhooks++;
    }
    if (nhooks != 4U) return 1;
    if (new_hooks[expected_position].token != 99U ||
        new_hooks[expected_position].priv != 999U) return 2;
    for (j = 0U; j < 3U; j++) {
        unsigned int slot = j >= expected_position ? j + 1U : j;
        if (new_hooks[slot].token != old_ops[j]->token ||
            new_hooks[slot].priv != 100U + j) return 3;
    }
    return 0;
}

int main(int argc, char **argv) {
    unsigned int extra = argc == 0;
    (void)argv;
    if (check_insert(5U + extra, 0U) != 0) return 11;
    if (check_insert(15U + extra, 1U) != 0) return 12;
    if (check_insert(25U + extra, 2U) != 0) return 13;
    if (check_insert(35U + extra, 3U) != 0) return 14;
    return 0;
}

struct perf_branch_entry {
    unsigned long from;
};

typedef int perf_snapshot_branch_stack_t(struct perf_branch_entry *entries, unsigned int cnt);
typedef int (*perf_snapshot_branch_stack_ptr_t)(struct perf_branch_entry *entries, unsigned int cnt);

extern typeof(perf_snapshot_branch_stack_t) __SCT__perf_snapshot_branch_stack;
extern perf_snapshot_branch_stack_t typed_direct;
extern int typed_direct(struct perf_branch_entry *entries, unsigned int cnt);
extern perf_snapshot_branch_stack_t (parenthesized_direct);
extern perf_snapshot_branch_stack_ptr_t callback_slot;

int invoke_typed(struct perf_branch_entry *entries) {
    return __SCT__perf_snapshot_branch_stack(entries, 1U) + typed_direct(entries, 2U) +
           parenthesized_direct(entries, 3U) + callback_slot(entries, 4U);
}

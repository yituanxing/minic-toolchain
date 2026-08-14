struct perf_branch_entry {
    unsigned long from;
};

typedef int perf_snapshot_branch_stack_t(struct perf_branch_entry *entries, unsigned int cnt);
typedef int (*perf_snapshot_branch_stack_ptr_t)(struct perf_branch_entry *entries,
                                                unsigned int cnt);

extern typeof(perf_snapshot_branch_stack_t) __SCT__perf_snapshot_branch_stack;
extern perf_snapshot_branch_stack_t typed_direct;
extern int typed_direct(struct perf_branch_entry *entries, unsigned int cnt);
extern perf_snapshot_branch_stack_t(parenthesized_direct);
extern perf_snapshot_branch_stack_ptr_t callback_slot;

int invoke_typed(struct perf_branch_entry *entries) {
    return __SCT__perf_snapshot_branch_stack(entries, 1U) + typed_direct(entries, 2U) +
           parenthesized_direct(entries, 3U) + callback_slot(entries, 4U);
}

struct p_log;
struct fs_parameter_spec;
struct fs_parameter;
struct fs_parse_result;

typedef int fs_param_type(struct p_log *log,
                          const struct fs_parameter_spec *spec,
                          struct fs_parameter *param,
                          struct fs_parse_result *result);

fs_param_type fs_param_is_bool, fs_param_is_u32, fs_param_is_s32, fs_param_is_u64, fs_param_is_enum,
    fs_param_is_string, fs_param_is_blob, fs_param_is_blockdev, fs_param_is_path, fs_param_is_fd;

int invoke_function_typed_list(struct p_log *log,
                               const struct fs_parameter_spec *spec,
                               struct fs_parameter *param,
                               struct fs_parse_result *result) {
    return fs_param_is_bool(log, spec, param, result) + fs_param_is_fd(log, spec, param, result);
}

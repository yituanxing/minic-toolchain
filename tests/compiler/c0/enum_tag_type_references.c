struct hrtimer;
struct fwnode_handle;

enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

extern enum system_states_like {
    SYSTEM_BOOTING_LIKE,
    SYSTEM_RUNNING_LIKE,
} system_state_like;

typedef enum system_states_like system_state_alias;

/* Linux timer.h shape: first use is a function return type, definition later. */
extern enum hrtimer_restart it_real_fn(struct hrtimer *);

/* Linux trace shape: first use is a function-pointer typedef return type. */
typedef enum print_line_t (*trace_print_func)(void);

/* Linux security.h shape: explicit standalone incomplete enum declaration. */
enum fs_value_type;

/* Linux fwnode.h shape: first use is a function-pointer record field return type. */
struct fwnode_operations_like {
    enum dev_dma_attr (*device_get_dma_attr)(const struct fwnode_handle *fwnode);
};

enum hrtimer_restart {
    HRTIMER_NORESTART,
    HRTIMER_RESTART,
};

enum print_line_t {
    TRACE_TYPE_PARTIAL_LINE,
    TRACE_TYPE_HANDLED,
};

enum fs_value_type {
    FS_VALUE_UNDEFINED,
    FS_VALUE_FLAG,
};

enum dev_dma_attr {
    DEV_DMA_NOT_SUPPORTED,
    DEV_DMA_NON_COHERENT,
};

extern void add_taint_like(unsigned flag, enum lockdep_like state);
enum lockdep_like report_bug_like(unsigned long address, enum lockdep_like state);

static enum lockdep_like normalize_state(enum lockdep_like state) {
    return state;
}

static enum hrtimer_restart timer_result(void) {
    return HRTIMER_RESTART;
}

static enum fs_value_type fs_value(void) {
    return FS_VALUE_FLAG;
}

int main(void) {
    system_state_alias state = SYSTEM_BOOTING_LIKE;
    trace_print_func printer = (trace_print_func)0;
    struct fwnode_operations_like ops = {0};

    return normalize_state(LOCKDEP_LIKE_OK) + state + timer_result() + fs_value() +
           (printer != 0) + (ops.device_get_dma_attr != 0);
}

#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC must point to the compiler binary}"

work_dir="${BUILD_DIR:-build/core-ir-shadow}/pipeline-shadow"
rm -rf "$work_dir"
mkdir -p "$work_dir"

check_strict_case() {
    local name="$1"

    "$MINIC" -S "$work_dir/$name.i" -o "$work_dir/$name-normal.s"
    MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/$name.i" -o "$work_dir/$name-shadow.s"
    cmp "$work_dir/$name-normal.s" "$work_dir/$name-shadow.s"
}

cat >"$work_dir/supported.i" <<'EOF'
int main(void) {
    return 1 + 2;
}
EOF
check_strict_case supported

cat >"$work_dir/local-object.i" <<'EOF'
int main(void) {
    int value = 1;
    return value;
}
EOF
check_strict_case local-object

cat >"$work_dir/volatile-object.i" <<'EOF'
int main(void) {
    volatile int value = 1;
    return value;
}
EOF
check_strict_case volatile-object

cat >"$work_dir/parameter.i" <<'EOF'
int add_one(int value) {
    return value + 1;
}
EOF
check_strict_case parameter

cat >"$work_dir/pointer-parameter.i" <<'EOF'
int pointer_parameter(int *value) {
    return 7;
}
EOF

"$MINIC" -S "$work_dir/pointer-parameter.i" -o "$work_dir/pointer-parameter-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/pointer-parameter.i" \
    -o "$work_dir/pointer-parameter-shadow.s"
cmp "$work_dir/pointer-parameter-normal.s" "$work_dir/pointer-parameter-shadow.s"
MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/pointer-parameter.i" \
    -o "$work_dir/pointer-parameter-strict.s"
cmp "$work_dir/pointer-parameter-normal.s" "$work_dir/pointer-parameter-strict.s"

cat >"$work_dir/qualified-parameter.i" <<'EOF'
unsigned long qualified_parameter(const unsigned long value) {
    return value;
}
EOF
check_strict_case qualified-parameter

cat >"$work_dir/pointee-const-parameter.i" <<'EOF'
int pointee_const_parameter(const int *value) {
    return *value;
}
EOF
check_strict_case pointee-const-parameter

cat >"$work_dir/volatile-parameter-unsupported.i" <<'EOF'
unsigned long volatile_parameter(volatile unsigned long value) {
    return value;
}
EOF
"$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \
    -o "$work_dir/volatile-parameter-unsupported-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \
    -o "$work_dir/volatile-parameter-unsupported-shadow.s"
cmp "$work_dir/volatile-parameter-unsupported-normal.s" \
    "$work_dir/volatile-parameter-unsupported-shadow.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/volatile-parameter-unsupported.i" \
    -o "$work_dir/volatile-parameter-unsupported-strict.s" \
    2>"$work_dir/volatile-parameter-unsupported-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted a volatile parameter" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'volatile_parameter'" \
    "$work_dir/volatile-parameter-unsupported-strict.err" >/dev/null

cat >"$work_dir/mixed-add.i" <<'EOF'
int add_mixed(signed char value) {
    return value + 1;
}
EOF
check_strict_case mixed-add

cat >"$work_dir/implicit-conversion.i" <<'EOF'
int widen(signed char value) {
    return value;
}

signed char narrow(void) {
    return 257;
}
EOF
check_strict_case implicit-conversion

cat >"$work_dir/explicit-conversion.i" <<'EOF'
int truncate(unsigned long value) {
    return (int)value;
}
EOF
check_strict_case explicit-conversion

cat >"$work_dir/if-return.i" <<'EOF'
int select_return(int condition) {
    if (condition) {
        return 1;
    } else {
        return 2;
    }
}
EOF
check_strict_case if-return

cat >"$work_dir/if-assign-return.i" <<'EOF'
int select_assigned_return(int condition) {
    int value;
    if (condition) {
        value = 1;
        return value;
    } else {
        value = 2;
        return value;
    }
}
EOF
check_strict_case if-assign-return

cat >"$work_dir/if-empty-merge.i" <<'EOF'
int pass_through(int condition) {
    if (condition) {
    }
    return 7;
}
EOF
check_strict_case if-empty-merge

cat >"$work_dir/if-merge.i" <<'EOF'
int select_value(int condition) {
    int value;
    if (condition) {
        value = 1;
    } else {
        value = 2;
    }
    return value;
}
EOF
check_strict_case if-merge

cat >"$work_dir/while-backedge.i" <<'EOF'
int clear_then_add(int value) {
    while (value) {
        value = 0;
    }
    return value + 7;
}
EOF
check_strict_case while-backedge

cat >"$work_dir/while-continue.i" <<'EOF'
int while_continue(int value) {
    while (value) {
        if (value == 1) {
            value = 0;
            continue;
        }
        value = 1;
    }
    return value;
}
EOF
check_strict_case while-continue

cat >"$work_dir/for-shape.i" <<'EOF'
int for_shape(int value) {
    for (; value;) {
        value = 0;
    }
    return value;
}
EOF

check_strict_case for-shape

cat >"$work_dir/for-continue.i" <<'EOF'
int for_continue(int value) {
    for (; value; value = value - 1) {
        if (value == 2)
            continue;
    }
    return value;
}
EOF
check_strict_case for-continue

cat >"$work_dir/void-statement-expression.i" <<'EOF'
void sink(void);

void void_statement_expression(void) {
    ({ sink(); });
    return;
}
EOF
check_strict_case void-statement-expression

cat >"$work_dir/direct-call-v0.i" <<'EOF'
int direct_callee(int value) {
    return value + 1;
}

int direct_caller(int value) {
    return direct_callee(value);
}
EOF
check_strict_case direct-call-v0

cat >"$work_dir/direct-void-call-v0.i" <<'EOF'
void direct_sink(int value) {
    return;
}

void direct_void_caller(int value) {
    direct_sink(value);
    return;
}
EOF
check_strict_case direct-void-call-v0

cat >"$work_dir/direct-pointer-result-statement-v0.i" <<'EOF'
int *external_pointer_identity(int *value);

void consume_pointer_call(int *value) {
    external_pointer_identity(value);
    return;
}
EOF
check_strict_case direct-pointer-result-statement-v0

cat >"$work_dir/field-address-v0.i" <<'EOF'
struct pair {
    int value;
    long other;
};

void consume_int_pointer(int *value);

void pointer_field_address(struct pair *pair) {
    consume_int_pointer(&pair->value);
    return;
}
EOF
check_strict_case field-address-v0

cat >"$work_dir/scalar-is-zero-v0.i" <<'EOF'
int logical_not_int(int value) {
    return !value;
}

int logical_not_pointer(int *value) {
    return !value;
}

int logical_double_not(int value) {
    return !!value;
}
EOF
check_strict_case scalar-is-zero-v0

cat >"$work_dir/integer-negate-v0.i" <<'EOF'
int negate_int(int value) {
    return -value;
}

long negate_long(long value) {
    return -value;
}

int source_value(void);

int negate_call_result(void) {
    return -source_value();
}

int double_negate(int value) {
    return -(-value);
}
EOF
check_strict_case integer-negate-v0

cat >"$work_dir/indirect-call-unsupported.i" <<'EOF'
int indirect_caller(int (*callee)(int), int value) {
    return callee(value);
}
EOF
check_strict_case indirect-call-unsupported

cat >"$work_dir/const-record-member-subscript.i" <<'EOF'
struct pointer_view {
    int *items;
};

int *const_record_member_subscript(const struct pointer_view *view,
                                   unsigned long index) {
    return &view->items[index];
}
EOF
check_strict_case const-record-member-subscript

cat >"$work_dir/const-record-member-pointer-offset.i" <<'EOF'
struct offset_item {
    long value;
};

struct offset_view {
    struct offset_item *items;
};

struct offset_item *const_record_member_pointer_offset(const struct offset_view *view,
                                                       unsigned long index) {
    return view->items + index;
}
EOF
check_strict_case const-record-member-pointer-offset

cat >"$work_dir/scalar-return-cleanup.i" <<'EOF'
void cleanup_scalar(int *value) {
    *value = *value + 1;
}

int scalar_return_cleanup(int input) {
    int guard __attribute__((cleanup(cleanup_scalar))) = input;
    return guard + 7;
}
EOF
check_strict_case scalar-return-cleanup

cat >"$work_dir/bool-bitfield-storage-read.i" <<'EOF'
struct bool_bitfield_storage {
    unsigned int prefix : 1;
    _Bool ready : 1;
};

int bool_bitfield_storage_read(struct bool_bitfield_storage *state) {
    return !state->ready;
}
EOF
check_strict_case bool-bitfield-storage-read

cat >"$work_dir/bool-bitfield-storage-write.i" <<'EOF'
struct bool_bitfield_storage_write {
    unsigned int prefix : 1;
    _Bool ready : 1;
};

void bool_bitfield_storage_write(struct bool_bitfield_storage_write *state) {
    state->ready = 1;
    state->ready = 0;
}
EOF
check_strict_case bool-bitfield-storage-write

cat >"$work_dir/local-char-array-object.i" <<'EOF'
int local_char_array_object(int index) {
    char bytes[4];
    bytes[0] = 1;
    bytes[3] = 7;
    return bytes[index];
}
EOF
check_strict_case local-char-array-object

cat >"$work_dir/local-pointer-array-decay.i" <<'EOF'
struct local_pointer_array_item;
extern int consume_local_pointer_array(struct local_pointer_array_item **items);
int local_pointer_array_decay(struct local_pointer_array_item *item) {
    struct local_pointer_array_item *items[] = { item, (void *)0 };
    return consume_local_pointer_array(items);
}
EOF
check_strict_case local-pointer-array-decay

cat >"$work_dir/indirect-cfg-argument.i" <<'EOF'
int indirect_cfg_argument(int (*callee)(int), int value) {
    return callee(value ? value : 1);
}
EOF
check_strict_case indirect-cfg-argument

cat >"$work_dir/call-frame-address-level-zero.i" <<'EOF'
unsigned long core_return_address_level_zero(void) {
    return (unsigned long)__builtin_return_address(0);
}

unsigned long core_frame_address_level_zero(void) {
    return (unsigned long)__builtin_frame_address(0);
}
EOF
check_strict_case call-frame-address-level-zero

cat >"$work_dir/comma-discard-record-assignment.i" <<'EOF'
struct core_comma_pair { int a; int b; };
int comma_discard_record_assignment(void) {
    struct core_comma_pair value;
    return ((value = (struct core_comma_pair){ .a = 1, .b = 2 }), 1);
}
EOF
check_strict_case comma-discard-record-assignment

cat >"$work_dir/qualified-statement-conditional.i" <<'EOF'
struct core_cond_limits { unsigned int first; unsigned int second; };
unsigned int qualified_statement_conditional(const struct core_cond_limits *limits) {
    return ({
        __auto_type x = (limits->first);
        __auto_type y = (limits->second);
        x < y ? x : y;
    });
}
EOF
check_strict_case qualified-statement-conditional

cat >"$work_dir/terminating-switch-return.i" <<'EOF'
const char *terminating_switch_return(unsigned short tag) {
    switch (tag) {
    case 1: return "one";
    case 2: return "two";
    default: return "other";
    }
}
EOF
check_strict_case terminating-switch-return

cat >"$work_dir/function-pointer-parameter-ingress.i" <<'EOF'
typedef void (*core_callback_t)(void *);
struct core_param_mask { unsigned long bits[1]; };
extern struct core_param_mask core_param_online;
extern void core_call_mask(void *, core_callback_t, void *, _Bool, const struct core_param_mask *);
void function_pointer_parameter_ingress(core_callback_t func, void *info, int wait) {
    core_call_mask((void *)0, func, info, wait, &core_param_online);
}
EOF
check_strict_case function-pointer-parameter-ingress

cat >"$work_dir/omitted-conditional-pointee-const.i" <<'EOF'
struct core_xattr_handler {
    const char *prefix;
    const char *name;
};

const char *omitted_conditional_pointee_const(const struct core_xattr_handler *handler) {
    return handler->prefix ?: handler->name;
}
EOF
check_strict_case omitted-conditional-pointee-const

cat >"$work_dir/terminating-switch-fallthrough.i" <<'EOF'
int terminating_switch_fallthrough(int value) {
    switch (value) {
    case 0:
    case 1:
        return 11;
    case 2:
        return 22;
    default:
        return 33;
    }
}
EOF
check_strict_case terminating-switch-fallthrough

cat >"$work_dir/promoted-unary-call-argument.i" <<'EOF'
typedef unsigned short core_u16;
extern core_u16 core_u16_add(core_u16 left, core_u16 right);
core_u16 promoted_unary_call_argument(core_u16 left, core_u16 right) {
    return core_u16_add(left, ~right);
}
int promoted_unary_negate(core_u16 value) {
    return -value;
}
EOF
check_strict_case promoted-unary-call-argument

cat >"$work_dir/variadic-call-unsupported.i" <<'EOF'
int variadic_external(int first, ...);

int variadic_caller(int value) {
    return variadic_external(value, value);
}
EOF
check_strict_case variadic-call-unsupported

if MINIC_CORE_IR=invalid "$MINIC" -S "$work_dir/supported.i" \
    -o "$work_dir/invalid-mode.s" 2>"$work_dir/invalid-mode.err"; then
    echo "invalid Core IR shadow mode unexpectedly succeeded" >&2
    exit 1
fi
grep -F "MINIC_CORE_IR must be unset, 'shadow', or 'strict'" \
    "$work_dir/invalid-mode.err" >/dev/null

#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


# CoreValue is block-local.  A scalar RHS is allowed to create CFG, so do not
# materialize the assignment target address before lowering that RHS and then
# carry the address value across blocks.  C does not sequence the LHS address
# computation before the RHS value computation; MiniC deliberately chooses
# RHS-first here, then materializes the address in the actual continuation.
replace_once(
    "src/core/core_lower.c",
    '''    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    {
        MinicType stored_type;

        if (!minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
    '''    {
        MinicType stored_type;

        if (!minic_type_unqualified(target->type, &stored_type) ||
            !core_memory_scalar_type(stored_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
''',
    "Core M19 assignment continuation ownership",
)

replace_once(
    "tests/compiler/c0/core_logical_and_value.c",
    '''int core_m19_cfg_statement_rhs(int left, int right) {
    return left && ({
        if (right < 0)
            core_m19_rhs(0);
        right;
    });
}
''',
    '''int core_m19_cfg_statement_rhs(int left, int right) {
    return left && ({
        do {
            if (right == 0)
                right = 1;
        } while (0);
        right;
    });
}

int core_m19_cfg_initializer(int value) {
    int result = ({
        do {
            if (value == 0)
                value = 1;
        } while (0);
        value;
    });
    return result;
}
''',
    "Core M19 CFG regression shape",
)

replace_once(
    "tests/compiler/c0/core_logical_and_value_runtime.c",
    '''int core_m19_nested(int first, int second, int third);
int core_m19_cfg_statement_rhs(int left, int right);
int core_m19_list_empty_careful_shape(const struct core_m19_node *head);
''',
    '''int core_m19_nested(int first, int second, int third);
int core_m19_cfg_statement_rhs(int left, int right);
int core_m19_cfg_initializer(int value);
int core_m19_list_empty_careful_shape(const struct core_m19_node *head);
''',
    "Core M19 CFG runtime declaration",
)
replace_once(
    "tests/compiler/c0/core_logical_and_value_runtime.c",
    '''    printf("cfg=%d,%d,%d\\n",
           core_m19_cfg_statement_rhs(0, -1),
           core_m19_cfg_statement_rhs(1, 0),
           core_m19_cfg_statement_rhs(1, 7));
    printf("list=%d,%d\\n",
''',
    '''    printf("cfg=%d,%d,%d\\n",
           core_m19_cfg_statement_rhs(0, 0),
           core_m19_cfg_statement_rhs(1, 0),
           core_m19_cfg_statement_rhs(1, 7));
    printf("init=%d,%d\\n", core_m19_cfg_initializer(0), core_m19_cfg_initializer(7));
    printf("list=%d,%d\\n",
''',
    "Core M19 CFG runtime output",
)

replace_once(
    "tests/compiler/c0/run-core-logical-and-value.sh",
    '''              core_m19_get_rhs_calls core_m19_nested core_m19_cfg_statement_rhs \\
              core_m19_list_empty_careful_shape; do
''',
    '''              core_m19_get_rhs_calls core_m19_nested core_m19_cfg_statement_rhs \\
              core_m19_cfg_initializer core_m19_list_empty_careful_shape; do
''',
    "Core M19 CFG symbol contract",
)
replace_once(
    "tests/compiler/c0/run-core-logical-and-value.sh",
    '''grep -F 'cfg=0,0,1' "$work/minic.out" >/dev/null
grep -F 'list=1,0' "$work/minic.out" >/dev/null
''',
    '''grep -F 'cfg=0,1,1' "$work/minic.out" >/dev/null
grep -F 'init=1,7' "$work/minic.out" >/dev/null
grep -F 'list=1,0' "$work/minic.out" >/dev/null
''',
    "Core M19 CFG runtime contract",
)

print("staged M19 Core CFG assignment continuation ownership")

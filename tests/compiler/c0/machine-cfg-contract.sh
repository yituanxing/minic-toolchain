#!/bin/sh
# Sourced by C0 source-to-MachineIR assembly probes.  A retired AST emitter
# named blocks .L<function>_core_bb* / _core_return; the MachineIR GNU writer
# owns .Lminic_mir_<function>_<block-id>.  Check actual defined blocks and a
# real return instruction instead of pinning an obsolete label spelling.

require_machine_function_cfg() {
    assembly=$1
    function_name=$2
    minimum_blocks=$3
    case "$function_name" in
        ''|*[!a-zA-Z0-9_]*)
            printf '%s\n' "FAIL machine-CFG invalid function name: $function_name" >&2
            exit 1
            ;;
    esac
    block_count=$(grep -Ec "^\\.Lminic_mir_${function_name}_[0-9]+:$" "$assembly" || true)
    if [ "$block_count" -lt "$minimum_blocks" ] ||
       ! grep -Eq '^[[:space:]]+ret$' "$assembly"; then
        printf '%s\n' \
            "FAIL machine-CFG function=$function_name blocks=$block_count expected-at-least=$minimum_blocks ret=required source=$assembly" >&2
        exit 1
    fi
}

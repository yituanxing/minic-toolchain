#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

log_dir="$root/build/ci-logs"
apt_cache=${APT_CACHE_DIR:-"$HOME/.cache/minic-apt/archives"}
package_manifest="$root/tools/ci/ubuntu-24.04-packages.txt"
cache_hit=${RV64_CACHE_HIT:-false}
cpu_count=$(nproc)

mkdir -p "$log_dir"

names=()
pids=()
logs=()
starts=()

start_gate() {
    local name=$1
    shift
    local log="$log_dir/$name.log"

    names+=("$name")
    logs+=("$log")
    starts+=("$(date +%s)")
    (
        set -Eeuo pipefail
        "$@"
    ) >"$log" 2>&1 &
    pids+=("$!")
}

wait_phase() {
    local index
    local failed=0

    for index in "${!pids[@]}"; do
        local name=${names[$index]}
        local log=${logs[$index]}
        local elapsed

        if wait "${pids[$index]}"; then
            elapsed=$(( $(date +%s) - starts[$index] ))
            printf 'PASS ci/%s elapsed=%ss\n' "$name" "$elapsed"
            if ! grep -E '^(PASS|SKIP|PROFILE|TOOL|PACKAGE) ' "$log"; then
                tail -n 20 "$log"
            fi
        else
            elapsed=$(( $(date +%s) - starts[$index] ))
            printf 'FAIL ci/%s elapsed=%ss\n' "$name" "$elapsed" >&2
            cat "$log" >&2
            failed=1
        fi
    done

    names=()
pids=()
logs=()
starts=()
    return "$failed"
}

install_rv64_tools() {
    local cached_packages=()
    local requested_packages=()
    local package

    while IFS= read -r package || [[ -n "$package" ]]; do
        case "$package" in
        ''|'#'*)
            continue
            ;;
        esac
        requested_packages+=("$package")
    done <"$package_manifest"

    if (( ${#requested_packages[@]} == 0 )); then
        printf '%s\n' 'RISC-V package manifest is empty' >&2
        return 1
    fi

    mkdir -p "$apt_cache/partial"
    if [[ "$cache_hit" == true ]]; then
        while IFS= read -r -d '' package; do
            cached_packages+=("$package")
        done < <(find "$apt_cache" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z)
    fi

    if (( ${#cached_packages[@]} > 0 )); then
        printf 'Restoring %d cached RISC-V packages\n' "${#cached_packages[@]}"
        sudo apt-get install -y --no-install-recommends "${cached_packages[@]}"
    else
        printf 'Building the RISC-V package cache from %d requested packages\n' \
            "${#requested_packages[@]}"
        sudo apt-get update
        sudo apt-get \
            -o Dir::Cache::archives="$apt_cache" \
            -o APT::Keep-Downloaded-Packages=true \
            install -y --no-install-recommends \
            "${requested_packages[@]}"
    fi

    sudo chown -R "$USER:$USER" "$(dirname "$apt_cache")"
    sh tools/ci/verify-validation-tools.sh
}

source_inventory() {
    sh tools/maintenance/check-production-source-inventory.sh
}

format_check() {
    CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
}

host_debug() {
    make -j1 BUILD_DIR=build/ci-debug check
}

host_release() {
    make -j1 \
        MODE=release \
        BUILD_DIR=build/ci-release \
        CFLAGS=-Werror \
        check
}

host_sanitize() {
    local jobs=$(( cpu_count / 2 ))
    if (( jobs < 1 )); then
        jobs=1
    fi
    make -j"$jobs" \
        MODE=sanitize \
        BUILD_DIR=build/ci-sanitize \
        check
}

static_local_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-static-local-focused" \
        sh tests/compiler/c0/run-static-local-arrays.sh
}

predefined_func_name_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-predefined-func-name" \
        sh tests/compiler/c0/run-predefined-func-name.sh
}

static_aggregate_initializer_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-static-aggregate-initializers" \
        sh tests/compiler/c0/run-static-aggregate-initializers.sh
}

static_nested_record_designator_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-static-nested-record-designator" \
        sh tests/compiler/c0/run-static-nested-record-designator.sh
}

variadic_declaration_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-variadic-declarations" \
        sh tests/compiler/c0/run-variadic-declarations.sh
}

variadic_call_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-variadic-direct-calls" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
    REQUIRE_RISCV_RUNTIME=1 \
        sh tests/compiler/c0/run-variadic-direct-calls.sh
}

pointer_equality_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-pointer-equality" \
        sh tests/compiler/c0/run-pointer-equality.sh
}

switch_control_flow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-switch-control-flow" \
        sh tests/compiler/c0/run-switch-control-flow.sh
}

wide_string_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-wide-string" \
        sh tests/compiler/c0/run-wide-string-literal.sh
}

runtime_record_array_initializer_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-runtime-record-array-initializer" \
        sh tests/compiler/c0/run-runtime-record-array-initializers.sh
}

core_required_no_fallback_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-core-required-no-fallback" \
        sh tests/compiler/c0/run-core-required-no-fallback.sh
}

core_scalar_lvalue_bitcast_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-lvalue-bitcast" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-lvalue-bitcast.sh
}

core_fixed_call_scalar_conversions_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-fixed-call-scalar-conversions" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-fixed-call-scalar-conversions.sh
}

core_scalar_assignment_implicit_void_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-assignment-implicit-void" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-assignment-implicit-void.sh
}

core_global_scalar_memory_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-global-scalar-memory" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-global-scalar-memory.sh
}

core_integer_equality_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-equality" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-equality.sh
}

core_integer_multiply_overflow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-multiply-overflow" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh
}

core_integer_add_overflow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-add-overflow" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-add-overflow.sh
}

core_short_circuit_or_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-short-circuit-or" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-short-circuit-or.sh
}

core_nested_if_continuation_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-nested-if-continuation" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-nested-if-continuation.sh
}

core_condition_and_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-condition-and" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-condition-and.sh
}

core_logical_and_value_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-logical-and-value" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-logical-and-value.sh
}

core_integer_bitwise_and_assignment_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-bitwise-and-assignment" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-bitwise-and-assignment.sh
}

core_pointer_offset_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-pointer-offset" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-pointer-offset.sh
}

core_pointer_equality_qualifiers_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-pointer-equality-qualifiers" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-pointer-equality-qualifiers.sh
}

core_statement_expression_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-statement-expression" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-statement-expression.sh
}

core_opaque_inline_asm_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-opaque-inline-asm" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-opaque-inline-asm.sh
}

core_scalar_not_equal_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-scalar-not-equal" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-scalar-not-equal.sh
}

core_integer_subtract_overflow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-subtract-overflow" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-subtract-overflow.sh
}

core_target_constant_fallback_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-target-constant-fallback" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-target-constant-fallback.sh
}

core_integer_bitwise_not_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-bitwise-not" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-bitwise-not.sh
}

core_postfix_update_m24_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-postfix-update-m24" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-postfix-update-m24.sh
}

core_discard_expression_m25_focused() {
    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-discard-expression-m25" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-discard-expression-m25.sh
}

core_integer_binary_preservation_m25b_focused() {
    MINIC="$root/build/ci-debug/bin/minic" BUILD_DIR="$root/build/ci-core-integer-binary-preservation-m25b" RISCV_CC=riscv64-linux-gnu-gcc QEMU_RISCV64=qemu-riscv64 sh tests/compiler/c0/run-core-integer-binary-preservation-m25b.sh
}

core_integer_less_m26_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-less-m26" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-less-m26.sh
}

core_integer_foundation_m26b_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-foundation-m26b" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-foundation-m26b.sh
}


core_switch_m27_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-switch-m27" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-switch-m27.sh
}

core_inline_asm_identity_m28_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-inline-asm-identity-m28" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-inline-asm-identity-m28.sh
}

core_record_local_m29_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-record-local-m29" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-record-local-m29.sh
}

core_aggregate_boundary_m30_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-aggregate-boundary-m30" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-aggregate-boundary-m30.sh
}

runtime_record_fam_prefix_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-runtime-record-fam-prefix" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
    REQUIRE_RISCV_RUNTIME=1 \
        sh tests/compiler/c0/run-gnu-record-compound-literal.sh
}

linenoise_driven_focused() {
    local script
    for script in \
        run-short-integers.sh \
        run-function-pointer-parameters.sh \
        run-extern-objects.sh \
        run-function-type-typedefs.sh \
        run-unnamed-prototype-parameters.sh \
        run-static-pointer-arrays.sh \
        run-pointer-array-typed-null.sh \
        run-static-zero-definitions.sh \
        run-static-zero-declaration-list.sh \
        run-record-copy-array-members.sh \
        run-zero-aggregate-null.sh \
        run-record-local-initializers.sh \
        run-multiply-assignment.sh \
        run-static-local-string-arrays.sh \
        run-external-pointer-definitions.sh \
        run-adjacent-string-literals.sh \
        run-global-pointer-subscripts.sh; do
        MINIC="$root/build/ci-debug/bin/minic" \
        HOST_CC=cc \
        BUILD_DIR="$root/build/ci-linenoise-driven" \
            sh "$root/tests/compiler/c0/$script"
    done
}

sds_driven_focused() {
    local script
    for script in \
        run-packed-record-layout.sh \
        run-flexible-array-members.sh \
        run-inline-functions.sh \
        run-postfix-const.sh \
        run-long-long-types.sh \
        run-wide-integer-literals.sh \
        run-array-bound-constant-expressions.sh \
        run-for-declaration-initializers.sh \
        run-void-pointer-locals.sh \
        run-compound-assignment-expressions.sh \
        run-void-casts.sh \
        run-external-scalar-definitions.sh; do
        MINIC="$root/build/ci-debug/bin/minic" \
        HOST_CC=cc \
        BUILD_DIR="$root/build/ci-sds-driven" \
            sh "$root/tests/compiler/c0/$script"
    done
}

rv64_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-rv64-focused" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
    REQUIRE_RISCV_RUNTIME=1 \
        sh tests/compiler/c0/run-runtime.sh
}

rv64_programs() {
    MINIC="$root/build/ci-release/bin/minic" \
    BUILD_DIR="$root/build/ci-rv64-programs" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    RISCV_OBJDUMP=riscv64-linux-gnu-objdump \
    QEMU_RISCV64=qemu-riscv64 \
    REQUIRE_RISCV_RUNTIME=1 \
        sh tests/programs/c0/run.sh
}

external_tiny_aes() {
    MINIC="$root/build/ci-release/bin/minic" \
    BUILD_DIR="$root/build/ci-external" \
    RISCV_CC=riscv64-linux-gnu-gcc \
        sh tests/external/tiny-aes-c/probe.sh
}

external_tentative_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    HOST_CC=cc \
    BUILD_DIR="$root/build/ci-external-tentative" \
        sh tests/compiler/c0/run-external-tentative-definitions.sh
}

static_global_section_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-static-global-section" \
        sh tests/compiler/c0/run-static-global-object-section.sh
}

static_object_address_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-static-object-address" \
        sh tests/compiler/c0/run-static-object-address-relocation.sh
}

file_scope_basic_asm_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-file-scope-basic-asm" \
    HOST_CC=cc \
        sh tests/compiler/c0/run-file-scope-basic-asm.sh
}

external_cjson_frontier() {
    MINIC="$root/build/ci-release/bin/minic" \
    BUILD_DIR="$root/build/ci-external" \
    RISCV_CC=riscv64-linux-gnu-gcc \
        sh tests/external/cjson/probe.sh
}

printf 'Runner CPUs=%s\n' "$cpu_count"
printf '%s\n' 'Phase 1: source inventory, format policy, and RISC-V tool preparation'
start_gate source-inventory source_inventory
start_gate format-check format_check
start_gate rv64-tools install_rv64_tools
if ! wait_phase; then
    exit 1
fi

printf '%s\n' 'Phase 1b: three host configurations after target tools are ready'
start_gate host-debug host_debug
start_gate host-release-werror host_release
start_gate host-sanitize host_sanitize
if ! wait_phase; then
    exit 1
fi

printf '%s\n' \
    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/wide-string/record-array-init/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'
start_gate static-local-focused static_local_focused
start_gate predefined-func-name-focused predefined_func_name_focused
start_gate static-aggregate-initializer-focused static_aggregate_initializer_focused
start_gate static-nested-record-designator-focused static_nested_record_designator_focused
start_gate variadic-declarations-focused variadic_declaration_focused
start_gate variadic-call-focused variadic_call_focused
start_gate pointer-equality-focused pointer_equality_focused
start_gate switch-control-flow-focused switch_control_flow_focused
start_gate external-tentative-focused external_tentative_focused
start_gate static-global-section-focused static_global_section_focused
start_gate static-object-address-focused static_object_address_focused
start_gate file-scope-basic-asm-focused file_scope_basic_asm_focused
start_gate wide-string-focused wide_string_focused
start_gate record-array-init-focused runtime_record_array_initializer_focused
start_gate core-required-no-fallback-focused core_required_no_fallback_focused
start_gate core-scalar-lvalue-bitcast-focused core_scalar_lvalue_bitcast_focused
start_gate core-fixed-call-scalar-conversions-focused core_fixed_call_scalar_conversions_focused
start_gate core-scalar-assignment-implicit-void-focused core_scalar_assignment_implicit_void_focused
start_gate core-global-scalar-memory-focused core_global_scalar_memory_focused
start_gate core-integer-equality-focused core_integer_equality_focused
start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused
start_gate core-integer-add-overflow-focused core_integer_add_overflow_focused
start_gate core-short-circuit-or-focused core_short_circuit_or_focused
start_gate core-nested-if-continuation-focused core_nested_if_continuation_focused
start_gate core-condition-and-focused core_condition_and_focused
start_gate core-logical-and-value-focused core_logical_and_value_focused
start_gate core-scalar-not-equal-focused core_scalar_not_equal_focused
start_gate core-integer-bitwise-and-assignment-focused core_integer_bitwise_and_assignment_focused
start_gate core-pointer-offset-focused core_pointer_offset_focused
start_gate core-pointer-equality-qualifiers-focused core_pointer_equality_qualifiers_focused
start_gate core-statement-expression-focused core_statement_expression_focused
start_gate core-opaque-inline-asm-focused core_opaque_inline_asm_focused
start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused
start_gate core-target-constant-fallback-focused core_target_constant_fallback_focused
start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused
start_gate core-postfix-update-m24-focused core_postfix_update_m24_focused
start_gate core-discard-expression-m25-focused core_discard_expression_m25_focused
start_gate core-integer-binary-preservation-m25b-focused core_integer_binary_preservation_m25b_focused
start_gate core-integer-less-m26-focused core_integer_less_m26_focused
start_gate core-switch-m27-focused core_switch_m27_focused
start_gate core-inline-asm-identity-m28-focused core_inline_asm_identity_m28_focused
start_gate core-record-local-m29-focused core_record_local_m29_focused
start_gate core-aggregate-boundary-m30-focused core_aggregate_boundary_m30_focused
start_gate core-integer-foundation-m26b-focused core_integer_foundation_m26b_focused
start_gate record-fam-prefix-focused runtime_record_fam_prefix_focused
start_gate linenoise-driven-focused linenoise_driven_focused
start_gate sds-driven-focused sds_driven_focused
start_gate rv64-focused rv64_focused
start_gate rv64-programs rv64_programs
start_gate external-tiny-aes external_tiny_aes
start_gate external-cjson-frontier external_cjson_frontier
wait_phase

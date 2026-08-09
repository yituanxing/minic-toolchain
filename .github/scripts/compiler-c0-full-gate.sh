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

linenoise_driven_focused() {
    local script
    for script in \
        run-short-integers.sh \
        run-function-pointer-parameters.sh \
        run-extern-objects.sh \
        run-function-type-typedefs.sh \
        run-unnamed-prototype-parameters.sh \
        run-static-pointer-arrays.sh \
        run-static-zero-definitions.sh \
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

external_cjson_frontier() {
    MINIC="$root/build/ci-release/bin/minic" \
    BUILD_DIR="$root/build/ci-external" \
    RISCV_CC=riscv64-linux-gnu-gcc \
        sh tests/external/cjson/probe.sh
}

printf 'Runner CPUs=%s\n' "$cpu_count"
printf '%s\n' 'Phase 1: source inventory, format policy, tool preparation, and three host configurations'
start_gate source-inventory source_inventory
start_gate format-check format_check
start_gate rv64-tools install_rv64_tools
start_gate host-debug host_debug
start_gate host-release-werror host_release
start_gate host-sanitize host_sanitize
if ! wait_phase; then
    exit 1
fi

printf '%s\n' \
    'Phase 2: focused declaration/static-local/variadic-call/pointer-equality/switch/linenoise/SDS/RV64 suites, differential programs, tiny-AES, and cJSON'
start_gate static-local-focused static_local_focused
start_gate variadic-declarations-focused variadic_declaration_focused
start_gate variadic-call-focused variadic_call_focused
start_gate pointer-equality-focused pointer_equality_focused
start_gate switch-control-flow-focused switch_control_flow_focused
start_gate linenoise-driven-focused linenoise_driven_focused
start_gate sds-driven-focused sds_driven_focused
start_gate rv64-focused rv64_focused
start_gate rv64-programs rv64_programs
start_gate external-tiny-aes external_tiny_aes
start_gate external-cjson-frontier external_cjson_frontier
wait_phase

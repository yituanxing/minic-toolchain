#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

log_dir="$root/build/ci-logs"
apt_cache=${APT_CACHE_DIR:-"$HOME/.cache/minic-apt/archives"}
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
            if ! grep -E '^(PASS|SKIP) ' "$log"; then
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
    local packages=()

    mkdir -p "$apt_cache/partial"
    if [[ "$cache_hit" == true ]]; then
        while IFS= read -r -d '' package; do
            packages+=("$package")
        done < <(find "$apt_cache" -maxdepth 1 -type f -name '*.deb' -print0 | sort -z)
    fi

    if (( ${#packages[@]} > 0 )); then
        printf 'Restoring %d cached RISC-V packages\n' "${#packages[@]}"
        sudo apt-get install -y --no-install-recommends "${packages[@]}"
    else
        printf 'Building the RISC-V package cache\n'
        sudo apt-get update
        sudo apt-get \
            -o Dir::Cache::archives="$apt_cache" \
            -o APT::Keep-Downloaded-Packages=true \
            install -y --no-install-recommends \
                gcc-riscv64-linux-gnu \
                libc6-dev-riscv64-cross \
                qemu-user
    fi

    sudo chown -R "$USER:$USER" "$(dirname "$apt_cache")"
    riscv64-linux-gnu-gcc --version | sed -n '1p'
    qemu-riscv64 --version | sed -n '1p'
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

printf '%s\n' 'Phase 2: two RV64 suites plus the first external frontier'
start_gate rv64-focused rv64_focused
start_gate rv64-programs rv64_programs
start_gate external-tiny-aes external_tiny_aes
wait_phase

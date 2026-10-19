#!/bin/sh
# Original pinned cJSON 1.7.19, second real project after tinyAES:
# reuse the existing vendor/header/preprocessing/runtime/upstream gates
# from probe.sh and final_differential.sh. Only the compiler's target
# selection, external GNU toolchain and QEMU change per architecture.
# Discovery mode reports BLOCKED without claiming acceptance.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual built minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
vendor="$root/tests/vendor/cjson/upstream"
fixture="$root/tests/external/cjson/runtime_harness.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
passed=0
blocked=0
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64)
            gcc=aarch64-linux-gnu-gcc
            qemu=qemu-aarch64
            ;;
        loongarch64)
            gcc=loongarch64-linux-gnu-gcc-13
            qemu=qemu-loongarch64
            ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    # The original upstream C translation unit and the EXISTING full
    # cJSON runtime harness are the independent cross-GNU/QEMU oracle.
    "$gcc" -std=c11 -O0 -static -I"$vendor" \
        "$vendor/cJSON.c" "$fixture" -lm \
        -o "$work/$target.gnu.elf"
    set +e
    timeout 60s "$qemu" "$work/$target.gnu.elf" \
        >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    oracle_status=$?
    set -e
    if [ "$oracle_status" -ne 0 ]; then
        echo "FOUR_BACKEND_CJSON_ORACLE=FAIL target=$target exit=$oracle_status" >&2
        sed -n '1,12p' "$work/$target.gnu.err" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_CJSON_GNU=PASS target=$target original_source=1 full_runtime_harness=1 QEMU_exit=0"

    # Reuse existing cJSON vendor SHA checks, controlled std headers,
    # preprocessor flags and exact frozen preprocessed-source line checks.
    # The original probe invokes MINIC -S <file.i> -o <file.s> so this
    # adapter only adds --target; never delegates C compilation to GNU.
    mkdir -p "$work/$target/bin"
    cat >"$work/$target/bin/minic-target" <<'SH'
#!/bin/sh
exec "$MINIC_CC" --target="$MINIC_TARGET" "$@"
SH
    chmod +x "$work/$target/bin/minic-target"
    set +e
    MINIC_TARGET="$target-linux" MINIC="$work/$target/bin/minic-target" \
        RISCV_CC="$gcc" QEMU_RISCV64="$qemu" \
        BUILD_DIR="$work/$target" CORE_FAST_TRACE=0 MINIC_M1_TRACE=1 \
        sh "$root/tests/external/cjson/probe.sh" \
        >"$work/$target.probe.out" 2>"$work/$target.probe.err"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        if [ "$status" -ge 128 ]; then
            echo "FOUR_BACKEND_CJSON=FAIL target=$target stage=MiniC_crash exit=$status" >&2
            sed -n '1,18p' "$work/$target.probe.err" >&2
            exit 1
        fi
        if grep -q 'FAIL external/cjson: preprocessed line\|FAIL external/cjson: .*blob=' \
                "$work/$target.probe.err"; then
            echo "FOUR_BACKEND_CJSON=FAIL target=$target stage=source_identity_or_preprocess" >&2
            sed -n '1,9p' "$work/$target.probe.err" >&2
            exit 1
        fi
        # The historical probe prints a generic FAIL line first.
        # Report the first actual MiniC source/Core error instead.
        error_line=$(grep -m 1 'error:' "$work/$target.probe.err" || true)
        if [ -z "$error_line" ]; then
            error_line=$(grep -m 1 'FAIL external/cjson:' \
                "$work/$target.probe.err" || true)
        fi
        echo "FOUR_BACKEND_CJSON=BLOCKED target=$target stage=MiniC_compile_or_runtime status=$status input=original_cJSON_1.7.19"
        printf 'FOUR_BACKEND_CJSON_FIRST_DIAGNOSTIC target=%s %s\n' \
            "$target" "$error_line"
        # The backend reports the first failed C guard with precise Core context.
        grep -m 1 'MINIC_BACKEND_REJECT' "$work/$target.probe.err" || true
        grep -m 1 'MINIC_M1_FRONTIER' "$work/$target.probe.err" || true
        grep -m 1 'MINIC_M4_GLOBAL_FRONTIER' "$work/$target.probe.err" || true
        grep -m 1 'MINIC_M8_.*REJECT\|MINIC_M8_FRAME_CAP' "$work/$target.probe.err" || true
        # Bounded cJSON-only backend trace. Earlier 16-line stderr truncation
        # hid the true failing instruction for the 406-instruction function.
        # The last emitted CFG line is the first rejected block if the
        # preflight exits before machine instruction emission.
        grep 'MINIC_CJSON_CFG' "$work/$target.probe.err" | tail -n 4 || true
        grep 'MINIC_CJSON_STAGE' "$work/$target.probe.err" | tail -n 8 || true
        grep 'MINIC_CJSON_STEP' "$work/$target.probe.err" | tail -n 6 || true
        # Do not repeatedly dump successful ensure/compare_double/print_number
        # traces. The first backend rejection above identifies the actual
        # blocked function and Core guard; retain bounded generic context.
        echo "FOUR_BACKEND_CJSON_BOUNDED_STDERR target=$target begin"
        sed -n '1,16p' "$work/$target.probe.err"
        echo "FOUR_BACKEND_CJSON_BOUNDED_STDERR target=$target end"
        # Source and failure diagnostics survive inside the temporary
        # scratch only until this invocation exits; they are not a pass.
        blocked=$((blocked + 1))
        continue
    fi

    # Existing probe's runtime acceptance succeeded. Do NOT stop there:
    # run the untouched upstream test.c GCC-vs-MiniC byte-exact gate.
    if ! RISCV_CC="$gcc" QEMU_RISCV64="$qemu" \
            BUILD_DIR="$work/$target" \
            sh "$root/tests/external/cjson/final_differential.sh" \
            >"$work/$target.upstream.out" 2>"$work/$target.upstream.err"; then
        echo "FOUR_BACKEND_CJSON=FAIL target=$target stage=original_upstream_test_differential" >&2
        sed -n '1,18p' "$work/$target.upstream.err" >&2
        failed=$((failed + 1))
        continue
    fi
    # The original existing runtime probe is a success only after
    # MiniC cJSON.o is fully built, statically linked and run.
    echo "FOUR_BACKEND_CJSON=PASS target=$target original_source=1 GNU_AS_LD=1 full_runtime=1 original_upstream_test=1 QEMU_byte_exact=1"
    passed=$((passed + 1))
done
echo "FOUR_BACKEND_CJSON_FRONTIER=COMPLETE architectures=2 verified_runtime=$passed blocked=$blocked failed=$failed"
if [ "$failed" -ne 0 ]; then exit 1; fi
if [ "${FOUR_BACKEND_CJSON_REQUIRE:-0}" = "1" ]; then
    if [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; then
        echo "FOUR_BACKEND_CJSON_ACCEPT=FAIL passed=$passed blocked=$blocked" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_CJSON_ACCEPT=PASS targets=2 original_cJSON_1_7_19=1 GNU_QEMU_diff=1"
fi

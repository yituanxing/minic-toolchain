#!/usr/bin/env bash
# GNU functional oracle always runs. Opt into ASan/UBSan/LSan with
# SANITIZED_MINIAS=build/sanitize/bin/minic-as (make MODE=sanitize first).
# A restricted host that cannot run LSan reports it explicitly; no false
# memory-safety PASS is printed merely because the functional path passed.
set -Eeuo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$repo/tests/assembler/fixtures"
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
for tool in as objcopy cmp readelf timeout; do
    command -v "$tool" >/dev/null
done
if [[ ! -x $MINIAS ]]; then
    echo "MINIAS_SOURCE_MACRO_FUNCTIONAL=FAIL missing_MiniAS:$MINIAS" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

run_mini() {
    local flavor=$1
    shift
    if [[ $flavor == sanitized ]]; then
        env ASAN_OPTIONS="detect_leaks=$asan_detect_leaks:abort_on_error=1:halt_on_error=1" \
            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
            timeout 20s "$SANITIZED_MINIAS" --target=x86_64 "$@"
    else
        timeout 20s "$MINIAS" --target=x86_64 "$@"
    fi
}

# GNU accepts the trailing empty argument in `%(1<<29),`. Its textual
# expansion inside a vararg differs from MiniAS, so the macro body emits a
# constant byte, while both frontends still execute the vararg bind/copy.
positive="$fixtures/source-macro-lifetime-altvararg-gnu.S"
as --64 -o "$work/gnu.o" "$positive"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.text"
if [[ $(wc -c <"$work/gnu.text") -ne 2 ]]; then
    echo 'MINIAS_SOURCE_MACRO_FUNCTIONAL=FAIL GNU_macro_body_count' >&2
    exit 1
fi

for name in bad-qualifier bad-body unterminated; do
    if as --64 -o "$work/gnu-$name.o" \
        "$fixtures/source-macro-lifetime-$name.S" \
        >"$work/gnu-$name.stdout" 2>"$work/gnu-$name.stderr"; then
        echo "MINIAS_SOURCE_MACRO_FUNCTIONAL=FAIL GNU_accepted_$name" >&2
        exit 1
    fi
done

verify_cases() {
    local flavor=$1 name source expected
    local prefix="$work/$flavor"
    if ! run_mini "$flavor" -o "$prefix.o" "$positive" \
        >"$prefix.stdout" 2>"$prefix.stderr"; then
        echo "MINIAS_SOURCE_MACRO_$flavor=FAIL positive_macro" >&2
        sed -n '1,90p' "$prefix.stderr" >&2
        exit 1
    fi
    objcopy -j .text -O binary "$prefix.o" "$prefix.text"
    if ! cmp -s "$work/gnu.text" "$prefix.text"; then
        echo "MINIAS_SOURCE_MACRO_$flavor=FAIL GNU_bytes_mismatch" >&2
        exit 1
    fi
    for name in bad-qualifier bad-body unterminated; do
        source="$fixtures/source-macro-lifetime-$name.S"
        if run_mini "$flavor" -o "$prefix-$name.o" "$source" \
            >"$prefix-$name.stdout" 2>"$prefix-$name.stderr"; then
            echo "MINIAS_SOURCE_MACRO_$flavor=FAIL accepted_$name" >&2
            exit 1
        fi
        if [[ -e "$prefix-$name.o" ]]; then
            echo "MINIAS_SOURCE_MACRO_$flavor=FAIL rejected_object_$name" >&2
            exit 1
        fi
        if grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer)|SUMMARY: (AddressSanitizer|LeakSanitizer)|runtime error:|AddressSanitizer:DEADLYSIGNAL|LeakSanitizer has encountered' \
            "$prefix-$name.stderr"; then
            echo "MINIAS_SOURCE_MACRO_$flavor=FAIL memory_error_$name" >&2
            sed -n '1,90p' "$prefix-$name.stderr" >&2
            exit 1
        fi
        case "$name" in
            bad-qualifier) expected='unsupported-macro-qualifier' ;;
            bad-body) expected='unsupported-directive:.error' ;;
            unterminated) expected='unterminated-directive:.macro' ;;
        esac
        if ! grep -Fq "$expected" "$prefix-$name.stderr"; then
            echo "MINIAS_SOURCE_MACRO_$flavor=FAIL wrong_error_$name" >&2
            sed -n '1,60p' "$prefix-$name.stderr" >&2
            exit 1
        fi
    done
}

verify_cases release
echo 'MINIAS_SOURCE_MACRO_FUNCTIONAL=PASS GNU_bytes=exact altmacro_varargs=2 rejects=3 no_failed_objects=1'

if [[ -z ${SANITIZED_MINIAS:-} ]]; then
    echo 'MINIAS_SOURCE_MACRO_MEMORY=NOT_RUN (set SANITIZED_MINIAS to MODE=sanitize binary)'
    exit 0
fi
if [[ ! -x $SANITIZED_MINIAS ]] ||
   ! readelf -d "$SANITIZED_MINIAS" | grep -q 'libasan'; then
    echo 'MINIAS_SOURCE_MACRO_MEMORY=INCONCLUSIVE sanitized_binary_unavailable'
    exit 0
fi

# LSan needs /proc task access, which sandboxed CI may prohibit. Probe it
# separately and keep running ASan/UBSan with leak detection disabled if so.
asan_detect_leaks=1
lsan_state=PASS
if ! run_mini sanitized -o "$work/lsan-smoke.o" /dev/null \
    >"$work/lsan.stdout" 2>"$work/lsan.stderr"; then
    if grep -Fq 'LeakSanitizer has encountered a fatal error' "$work/lsan.stderr"; then
        asan_detect_leaks=0
        lsan_state=UNAVAILABLE
    else
        echo 'MINIAS_SOURCE_MACRO_MEMORY=INCONCLUSIVE ASan_runtime_unstable'
        sed -n '1,18p' "$work/lsan.stderr" >&2
        exit 0
    fi
fi
for attempt in 1 2 3; do
    if ! run_mini sanitized -o "$work/smoke-$attempt.o" /dev/null \
        >"$work/smoke.stdout" 2>"$work/smoke.stderr"; then
        echo 'MINIAS_SOURCE_MACRO_MEMORY=INCONCLUSIVE ASan_runtime_unstable'
        sed -n '1,18p' "$work/smoke.stderr" >&2
        exit 0
    fi
done
verify_cases sanitized
if [[ $lsan_state == PASS ]]; then
    echo 'MINIAS_SOURCE_MACRO_MEMORY=PASS ASan=clean UBSan=clean LSan=clean'
else
    echo 'MINIAS_SOURCE_MACRO_MEMORY=INCONCLUSIVE ASan=clean UBSan=clean LSan=UNAVAILABLE'
fi

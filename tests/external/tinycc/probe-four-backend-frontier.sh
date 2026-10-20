#!/bin/sh
# Original TinyCC 0.9.27, dual MiniC ISA compiler-object frontier.
# AArch64 runs TinyCC's original ARM64 backend. TinyCC 0.9.27 does not
# natively target LoongArch: that host builds a TinyCC x86-64 cross compiler.
# No false claim of LoongArch TCC codegen or executable acceptance.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
commit=d348a9a51d32cece842b7885d27a411436d7887b
mkdir -p "$work/upstream"
if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
    "https://github.com/TinyCC/tinycc/archive/$commit.tar.gz" \
    -o "$work/tinycc.tar.gz"; then
    echo "FOUR_BACKEND_TINYCC=FAIL stage=download" >&2
    exit 1
fi
tar -xzf "$work/tinycc.tar.gz" -C "$work/upstream" --strip-components=1
src="$work/upstream"
test "$(git hash-object "$src/tcc.c")" = cd887d17670dfecf0f056bb49048d3780848edab
test "$(git hash-object "$src/tcc.h")" = cd679732c40ae05e79ee5b74d2b90eaa7f944b6e
test "$(git hash-object "$src/configure")" = 1ee3acb8c944ccc5083d6ce0df9d39d7c483047d
test "$(cat "$src/VERSION")" = 0.9.27
echo "FOUR_BACKEND_TINYCC_SOURCE=VERIFIED release=0.9.27 commit=$commit"

passed=0
blocked=0
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64)
            gcc=aarch64-linux-gnu-gcc
            qemu=qemu-aarch64
            cpu=aarch64
            defines=-DTCC_TARGET_ARM64
            sources='tcc.c libtcc.c tccpp.c tccgen.c tccelf.c tccasm.c tccrun.c arm64-gen.c arm64-link.c'
            backend=arm64
            ;;
        loongarch64)
            gcc=loongarch64-linux-gnu-gcc-13
            qemu=qemu-loongarch64
            cpu=x86_64
            defines=-DTCC_TARGET_X86_64
            sources='tcc.c libtcc.c tccpp.c tccgen.c tccelf.c tccasm.c tccrun.c x86_64-gen.c x86_64-link.c i386-asm.c'
            backend=x86_64-cross
            ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    triplet=$("$gcc" -dumpmachine)
    count=0
    for f in $sources; do count=$((count+1)); done
    mkdir -p "$work/$target"
    # Original upstream configure generates config.h with a hosted x86 build
    # compiler; --cpu selects TCC's output ISA. GNU cross-compiler headers
    # and preprocessing remain the target machine's, not the host's.
    if ! (cd "$src" && ./configure --cc=cc --cpu="$cpu" \
              --triplet="$triplet" >"$work/$target/config.out" \
              2>"$work/$target/config.err"); then
        echo "FOUR_BACKEND_TINYCC=FAIL target=$target stage=upstream_configure" >&2
        tail -n 16 "$work/$target/config.err" >&2
        exit 1
    fi
    gnu_objects=""
    mixed_objects=""
    gnu_count=0
    target_compiled=0
    target_assembled=0
    target_blocked=0
    : >"$work/$target/rejects.log"
    for file in $sources; do
        stem=$(basename "$file" .c)
        input="$work/$target/$stem.i"
        asm="$work/$target/$stem.s"
        if ! "$gcc" -E -P -std=gnu11 -DONE_SOURCE=0 $defines \
                "-DCONFIG_TRIPLET=\"$triplet\"" -I"$src" \
                "$src/$file" -o "$input" \
                >"$work/$target/$stem.cpp.out" 2>"$work/$target/$stem.cpp.err"; then
            echo "FOUR_BACKEND_TINYCC=FAIL target=$target source=$file stage=GNU_preprocess" >&2
            tail -n 15 "$work/$target/$stem.cpp.err" >&2
            failed=$((failed+1))
            break
        fi
        if ! "$gcc" -O0 -std=gnu11 -c -x c "$input" \
                -o "$work/$target/$stem.gnu.o" \
                >"$work/$target/$stem.gnu.out" 2>"$work/$target/$stem.gnu.err"; then
            echo "FOUR_BACKEND_TINYCC=FAIL target=$target source=$file stage=GNU_oracle_compile" >&2
            tail -n 15 "$work/$target/$stem.gnu.err" >&2
            failed=$((failed+1))
            break
        fi
        # GNU reference objects use the same preprocessed bytes. Their
        # substitution is permitted in the HYBRID diagnostic only.
        gnu_objects="$gnu_objects $work/$target/$stem.gnu.o"
        gnu_count=$((gnu_count+1))
        set +e
        timeout 120s env MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 CORE_FAST_TRACE=1 \
            "$MINIC_CC" --target="$target-linux" -S "$input" \
            -o "$asm" >"$work/$target/$stem.mini.out" \
            2>"$work/$target/$stem.mini.err"
        rc=$?
        set -e
        if [ "$rc" -ne 0 ]; then
            if [ "$rc" -ge 128 ]; then
                echo "FOUR_BACKEND_TINYCC=FAIL target=$target source=$file stage=MiniC_crash rc=$rc" >&2
                failed=$((failed+1))
                break
            fi
            target_blocked=$((target_blocked+1))
            mixed_objects="$mixed_objects $work/$target/$stem.gnu.o"
            grep '^MINIC_BACKEND_REJECT' "$work/$target/$stem.mini.err" \
                >>"$work/$target/rejects.log" || true
            first=$(grep -m 1 -E 'MINIC_BACKEND_REJECT|error:' \
                "$work/$target/$stem.mini.err" || true)
            echo "FOUR_BACKEND_TINYCC_TU=BLOCKED target=$target source=$file stage=MiniC $first"
            # Classify semantic return-ABI gaps instead of treating each
            # TinyCC caller as a separate backend failure.
            grep -m 2 '^MINIC_CALL_RESULT_REJECT' "$work/$target/$stem.mini.err" || true
            # Core lowering errors originate BEFORE a target backend and
            # should not be mistaken for missing ISA opcodes.
            grep -m 2 -E '^CORE_FAST_|^CORE_IR_|^MINIC_CORE_' \
                "$work/$target/$stem.mini.err" || true
            continue
        fi
        target_compiled=$((target_compiled+1))
        if ! "$gcc" -c -x assembler "$asm" \
                -o "$work/$target/$stem.mini.o" \
                >"$work/$target/$stem.as.out" 2>"$work/$target/$stem.as.err"; then
            echo "FOUR_BACKEND_TINYCC=FAIL target=$target source=$file stage=GNU_assembler" >&2
            tail -n 16 "$work/$target/$stem.as.err" >&2
            failed=$((failed+1))
            break
        fi
        target_assembled=$((target_assembled+1))
        mixed_objects="$mixed_objects $work/$target/$stem.mini.o"
        echo "FOUR_BACKEND_TINYCC_TU=PASS target=$target source=$file GNU_C_oracle=1 MiniC_GNU_AS=1"
    done
    # Hybrid is a diagnostic only. GNU substitutes only currently blocked
    # TinyCC TUs, allowing GNU/QEMU checks of the compiled MiniC subset.
    if [ "$gnu_count" -eq "$count" ] &&
       [ "$((target_assembled + target_blocked))" -eq "$count" ]; then
        hybrid_status=0
        if ! "$gcc" -static $gnu_objects -lm -ldl \
                -o "$work/$target/tcc-gnu" \
                >"$work/$target/tcc-gnu.link.out" \
                2>"$work/$target/tcc-gnu.link.err"; then
            hybrid_status=1
            echo "FOUR_BACKEND_TINYCC_HYBRID=SKIP target=$target stage=GNU_reference_link"
            tail -n 14 "$work/$target/tcc-gnu.link.err" || true
        elif ! "$gcc" -static $mixed_objects -lm -ldl \
                -o "$work/$target/tcc-mixed" \
                >"$work/$target/tcc-mixed.link.out" \
                2>"$work/$target/tcc-mixed.link.err"; then
            hybrid_status=1
            echo "FOUR_BACKEND_TINYCC_HYBRID=BLOCKED target=$target stage=mixed_link"
            tail -n 14 "$work/$target/tcc-mixed.link.err" || true
        fi
        if [ "$hybrid_status" -eq 0 ]; then
            for builder in gnu mixed; do
                set +e
                timeout 45s "$qemu" "$work/$target/tcc-$builder" -v \
                    >"$work/$target/tcc-$builder.out" \
                    2>"$work/$target/tcc-$builder.err"
                status=$?
                set -e
                echo "FOUR_BACKEND_TINYCC_HYBRID_RUN target=$target builder=$builder status=$status"
                if [ "$status" -ne 0 ]; then hybrid_status=1; fi
            done
            if [ "$hybrid_status" -eq 0 ] &&
               cmp -s "$work/$target/tcc-gnu.out" \
                      "$work/$target/tcc-mixed.out" &&
               cmp -s "$work/$target/tcc-gnu.err" \
                      "$work/$target/tcc-mixed.err"; then
                echo "FOUR_BACKEND_TINYCC_HYBRID=PASS target=$target MiniC_objects=$target_assembled GNU_fallback_objects=$target_blocked GNU_QEMU_version_diff=1 full_MiniC_acceptance=NOT_CLAIMED"
            else
                echo "FOUR_BACKEND_TINYCC_HYBRID=BLOCKED target=$target stage=GNU_QEMU_version_diff MiniC_objects=$target_assembled GNU_fallback_objects=$target_blocked"
                diff -u "$work/$target/tcc-gnu.out" \
                        "$work/$target/tcc-mixed.out" | head -n 25 || true
                diff -u "$work/$target/tcc-gnu.err" \
                        "$work/$target/tcc-mixed.err" | head -n 25 || true
            fi
        fi
    fi
    if [ -s "$work/$target/rejects.log" ]; then
        awk '
          {
            stage="unknown"; kind="unknown";
            for (i=1;i<=NF;i++) {
              if ($i ~ /^stage=/) stage=$i;
              if ($i ~ /^kind=/) kind=$i;
            }
            count[stage " " kind]++;
          }
          END {
            for (key in count)
              print "FOUR_BACKEND_TINYCC_REJECT_CLASS", key, "count=" count[key];
          }' "$work/$target/rejects.log" | sort
    fi
    if [ "$target_blocked" -gt 0 ]; then
        blocked=$((blocked+1))
    elif [ "$target_assembled" -eq "$count" ]; then
        passed=$((passed+1))
    fi
    echo "FOUR_BACKEND_TINYCC_TARGET target=$target tcc_backend=$backend original_TUs=$count compiled=$target_compiled assembled=$target_assembled blocked=$target_blocked runtime=NOT_TESTED"
done
echo "FOUR_BACKEND_TINYCC_FRONTIER=COMPLETE object_sets=$passed blocked_targets=$blocked failed=$failed runtime_acceptance=NOT_CLAIMED"
if [ "$failed" -ne 0 ]; then exit 1; fi
# A full-object gate is available later; discovery does not pretend success.
if [ "$(printenv FOUR_BACKEND_TINYCC_REQUIRE_OBJECTS || true)" = 1 ] &&
   { [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; }; then
    echo "FOUR_BACKEND_TINYCC_OBJECTS=FAIL completed=$passed blocked=$blocked" >&2
    exit 1
fi
if [ "$passed" -eq 2 ]; then
    echo "FOUR_BACKEND_TINYCC_OBJECTS=PASS targets=2 pristine_source=1 GNU_AS=1 runtime_acceptance=NOT_CLAIMED"
fi

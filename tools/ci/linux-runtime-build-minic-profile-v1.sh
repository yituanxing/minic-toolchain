#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <build-dir> <evidence-dir>" >&2
  exit 64
fi

build_dir=$1
ev=$2
mkdir -p "$ev"

patches=(
  apply-local-integer-assignment-v2.py
  apply-local-integer-logical-conditions-v0.py
  apply-local-null-pointer-facts-v0.py
  apply-builtin-constant-p-local-facts-v0.py
  apply-inline-integer-specialization-v0.py
  apply-inline-specialization-local-facts-v0.py
  apply-inline-specialization-stable-parameter-facts-v0.py
  apply-inline-specialization-transitive-integer-v0.py
  apply-inline-specialization-base-callee-integer-v0.py
  apply-inline-specialization-capacity-v0.py
  apply-inline-specialization-symbolic-address-v0.py
  apply-inline-specialization-symbolic-nested-array-v0.py
  apply-inline-specialization-symbolic-integer-closure-v0.py
  apply-inline-specialization-symbolic-transitive-rewrite-v0.py
  apply-inline-specialization-symbolic-array-lvalue-v0.py
  apply-inline-specialization-symbolic-closed-integers-v0.py
  apply-inline-specialization-base-callee-symbolic-v0.py
  apply-inline-specialization-resolved-asm-goto-v0.py
  apply-inline-specialization-core-reachability-v0.py
  apply-inline-specialization-original-reachability-v0.py
  apply-inline-asm-local-integer-facts-v0.py
  apply-inline-asm-rk-early-v0.py
  apply-inline-asm-rk-early-hotfix-v0.py
  apply-inline-asm-rk-tail-rollback-v0.py
  apply-inline-asm-rk-cast-tail-v0.py
  apply-inline-asm-immediate-batch-trace-v0.py
  apply-inline-asm-write-output-trace-v0.py
  apply-inline-specialization-core-empty-function-v0.py
  apply-inline-specialization-label-alias-v0.py
  apply-riscv64-default-text-section-v0.py
  apply-riscv64-explicit-section-flags-v0.py
  apply-inline-asm-symbolic-specialization-v0.py
  apply-riscv64-core-value-slot-pack-v0.py
  apply-riscv64-core-value-slot-reuse-v0.py
  apply-riscv64-record-call-result-snapshot-reuse-v0.py
  apply-riscv64-core-object-slot-reuse-v0.py
  apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py
  apply-riscv64-structured-asm-used-callee-save-v0.py
  apply-riscv64-pi-local-symbol-address-v0.py
  apply-loop-mutated-specialization-fact-barrier-v0.py
  apply-local-asm-if-fact-barrier-v0.py
  apply-core-if-condition-dominance-barrier-v0.py
  apply-netlink-if-control-trace-v0.py
  apply-core-goto-label-fact-barrier-v0.py
  apply-runtime-if-fallthrough-facts-v1.py
)

start=$(date +%s%N)
: >"$ev/patch.log"
for p in "${patches[@]}"; do
  python3 "tools/ci/$p" >>"$ev/patch.log"
done
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build_dir" \
  "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >/dev/null
# Probe the exact Runtime-profile compiler before spending time producing
# hundreds of Linux objects. The old unpatched C0 MiniC is NOT the tested
# product here; this checks that a dead reference disappears while a dynamic
# reference is still retained (against unsound CFG fact propagation).
probe_dir="$ev/dma-constant-if-probe"
mkdir -p "$probe_dir"
cat >"$probe_dir/probe.c" <<'C'
extern int missing_arch_dma_set_uncached(void);
extern int must_retain_dynamic_call(void);
int dma_disabled_cfg(int coherent)
{
    _Bool set_uncached = 0, remap = 0;
    if (!coherent) {
        set_uncached = 0;
        remap = 0;
        if (!set_uncached && !remap)
            return 17;
    }
    if (set_uncached)
        return missing_arch_dma_set_uncached();
    return 23;
}
int dynamic_branch_preserved(int cond)
{
    _Bool x = 0;
    if (cond)
        x = 1;
    if (x)
        return must_retain_dynamic_call();
    return 0;
}
C
# Same path as Kbuild stage2: GNU preprocessing -> MiniC -S -> GNU AS.
# This runtime profile intentionally builds only minic and minic-cc (no minic-cpp).
riscv64-linux-gnu-gcc -E -P -x c "$probe_dir/probe.c" \
  -o "$probe_dir/probe.i"
"$build_dir/bin/minic" -S "$probe_dir/probe.i" -o "$probe_dir/probe.s"
riscv64-linux-gnu-gcc -x assembler -c "$probe_dir/probe.s" \
  -o "$probe_dir/probe.o"
riscv64-linux-gnu-nm -u "$probe_dir/probe.o" >"$probe_dir/undefined.txt"
if grep -Fq missing_arch_dma_set_uncached "$probe_dir/undefined.txt"; then
    echo "RUNTIME_DMA_BOOL_LINK_REFS=FAIL unresolved_dead_call" >&2
    exit 1
fi
if ! grep -Fq must_retain_dynamic_call "$probe_dir/undefined.txt"; then
    echo "RUNTIME_DMA_BOOL_LINK_REFS=FAIL erased_live_call" >&2
    exit 1
fi
echo "RUNTIME_DMA_BOOL_LINK_REFS=PASS dead_call_absent=1 live_call_present=1" | \
  tee "$probe_dir/verdict.txt"
end=$(date +%s%N)
echo "TIMING toolchain_ms=$(((end-start)/1000000))" | tee "$ev/toolchain-timing.txt"
printf '%s\n' "${patches[@]}" >"$ev/minic-profile.txt"
sha256sum "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >"$ev/minic.sha256"
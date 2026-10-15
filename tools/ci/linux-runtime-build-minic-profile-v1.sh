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
  # Explicit section flags now live in canonical RV64 C source.  The separate
  # default-text behavior still depends on the active Linux Runtime profile:
  # keep its existing apply script, which also installs the M179 guard facts.
  apply-riscv64-default-text-section-v0.py
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
  apply-runtime-stable-zero-bools-v0.py
)

start=$(date +%s%N)
: >"$ev/patch.log"
for p in "${patches[@]}"; do
  python3 "tools/ci/$p" >>"$ev/patch.log"
done
# Whole-Linux producer opt-in: run the exact two mechanisms proven on the
# eight-owner 24/24 P1 image across ALL actual Linux C translation units.
# Keep the ordinary Runtime profile unchanged.  The build is experimental
# and will be judged by 2064/2064 Kbuild + full Linux P0/shell/P1, not by
# synthetically expanding a microtest corpus.
if [[ "${MINIC_LINUX_FULL_CROSSBLOCK:-0}" == 1 ]]; then
  python3 tools/ci/apply-riscv64-p1-defined-before-use-crossblock-v1.py >>"$ev/patch.log"
  patches+=(apply-riscv64-p1-defined-before-use-crossblock-v1.py)
  [[ "${MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ:-0}" == 1 ]] || {
    echo "MINIC_LINUX_FULL_CROSSBLOCK=FAIL requires_discarded_local_eq=1" >&2
    exit 8
  }
  echo "MINIC_LINUX_FULL_CROSSBLOCK=ENABLED all_linux_C=true crossblock=experimental discarded_pure_comparison=1"
fi
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

/* Diagnostic only: a call in the outer guard may invalidate speculative
   local facts, but cannot change an unescaped automatic zero value. */
extern int unknown_dma_coherent(void);
extern int missing_dma_call_guard(void);
int dma_call_guard(void)
{
    _Bool set_uncached = 0, remap = 0;
    if (!unknown_dma_coherent()) {
        set_uncached = 0;
        remap = 0;
        if (!set_uncached && !remap)
            return 17;
    }
    if (set_uncached)
        return missing_dma_call_guard();
    return 23;
}

/* Diagnostic only: an explicit label can have multiple predecessor edges.
   Preserving zero must be justified at the join, not guessed by lexical
   fallthrough. */
extern int missing_dma_label_guard(void);
int dma_label_guard(int coherent, int must_jump)
{
    _Bool set_uncached = 0, remap = 0;
    if (!coherent) {
        set_uncached = 0;
        remap = 0;
        if (!set_uncached && !remap)
            return 17;
    }
    if (must_jump)
        goto setup_page;
setup_page:
    if (set_uncached)
        return missing_dma_label_guard();
    return 23;
}

/* MUST remain unresolved: a direct call that receives &local can mutate
   it even when the if body's control flow is trivial. */
extern int mutates_local_bool(_Bool *);
extern int must_retain_mutating_call(void);
int direct_call_escape_guard(void)
{
    _Bool set_uncached = 0;
    if (mutates_local_bool(&set_uncached))
        return 0;
    if (set_uncached)
        return must_retain_mutating_call();
    return 23;
}

/* Real kernel/dma/direct.c pattern: the only assignment of 1
   is unreachable because the feature-off guard must return first. */
extern int missing_dma_after_const_return(void);
int dma_const_return_guard(int coherent)
{
    _Bool set_uncached = 0;
    if (!coherent) {
        if (1)
            return 0;
        set_uncached = 1;
    }
    if (set_uncached)
        return missing_dma_after_const_return();
    return 23;
}

/* Exact relevant 6.6.143 shape: remap becomes 1 in a dynamic outer if,
   so the else assigning set_uncached must be unreachable. */
extern int missing_dma_remap_const_one(void);
int dma_real_remap_guard(int coherent)
{
    _Bool remap = 0, set_uncached = 0;
    if (!coherent) {
        remap = 1;
        if (remap) {
            if (!coherent)
                return 17;
        } else {
            if (!0)
                return 0;
            set_uncached = 1;
        }
    }
    if (set_uncached)
        return missing_dma_remap_const_one();
    return 23;
}

/* Regression for shmem_alloc_and_acct_folio(): the false edge of a
   terminating "if (err) goto failed" must retain huge=0 from the
   compile-time-true feature-disabled assignment. */
extern int missing_shmem_after_terminating_goto(void);
int shmem_disabled_huge_guard(int err)
{
    _Bool huge = 1;
    int nr;
    if (1)
        huge = 0;
    nr = huge ? 512 : 1;
    if (err)
        goto failed;
    if (huge)
        return missing_shmem_after_terminating_goto();
    return nr;
failed:
    return -2;
}

/* Negative oracle: the same CFG MUST retain the external reference when
   the local can really be set by a continuing dynamic true edge. */
extern int must_retain_after_conditional_goto(void);
int shmem_dynamic_huge_guard(int cond, int err)
{
    _Bool huge = 0;
    if (cond)
        huge = 1;
    if (err)
        goto failed;
    if (huge)
        return must_retain_after_conditional_goto();
    return 0;
failed:
    return -3;
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
if ! grep -Fq must_retain_mutating_call "$probe_dir/undefined.txt"; then
    echo "RUNTIME_DMA_BOOL_LINK_REFS=FAIL erased_mutating_call" >&2
    exit 1
fi
if grep -Fq missing_shmem_after_terminating_goto "$probe_dir/undefined.txt"; then
    echo "RUNTIME_SHMEM_CFG_GOTO=FAIL dead_call_retained" >&2
    exit 1
fi
if ! grep -Fq must_retain_after_conditional_goto "$probe_dir/undefined.txt"; then
    echo "RUNTIME_SHMEM_CFG_GOTO=FAIL live_call_erased" >&2
    exit 1
fi
echo "RUNTIME_SHMEM_CFG_GOTO=PASS dead_call_absent=1 live_call_present=1"
echo "RUNTIME_DMA_BOOL_LINK_REFS=PASS dead_call_absent=1 live_call_present=1" | \
  tee "$probe_dir/verdict.txt"
for symbol in missing_dma_call_guard missing_dma_label_guard missing_dma_after_const_return missing_dma_remap_const_one; do
  if grep -Fq "$symbol" "$probe_dir/undefined.txt"; then
    echo "RUNTIME_DMA_PROBE=RED symbol=$symbol class=needs_more_cfg_proof" | \
      tee -a "$probe_dir/verdict.txt"
  else
    echo "RUNTIME_DMA_PROBE=GREEN symbol=$symbol class=proven_eliminated" | \
      tee -a "$probe_dir/verdict.txt"
  fi
done

end=$(date +%s%N)
echo "TIMING toolchain_ms=$(((end-start)/1000000))" | tee "$ev/toolchain-timing.txt"
printf '%s\n' "${patches[@]}" >"$ev/minic-profile.txt"
sha256sum "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >"$ev/minic.sha256"
if [[ "${MINIC_LINUX_FULL_CROSSBLOCK:-0}" == 1 ]]; then
  # This is only a producer attestation. Actual correctness requires the
  # complete 2064/2064 Linux C pool plus real P0/P1 QEMU at the receiver.
  grep -Fqx 'apply-riscv64-p1-defined-before-use-crossblock-v1.py' "$ev/minic-profile.txt"
  test -s "$ev/minic.sha256"
  echo "LINUX_FULL_CROSSBLOCK=COMPILER_READY mode=all_linux_C opt_in_only=true compiler_sha=$(sha256sum "$build_dir/bin/minic" | cut -d' ' -f1)"
fi
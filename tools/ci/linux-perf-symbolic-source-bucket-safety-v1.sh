#!/usr/bin/env bash
# Qualify the already proven first500 candidate on real frozen Linux inputs,
# negative fixtures, optional allocation fallback and ASan/UBSan.
# Runs only in the disposable checkout of the existing perf500 CI job.
set -Eeuo pipefail
root="${1:?perf audit root}"
corpus="${2:?SHA-pinned first500 corpus}"
mkdir -p "$root/evidence" "$root/symbolic-source-call-safety"
test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"

# Rebuild exact previously qualified A/B compilers; do not duplicate 500.
MINIC_SYMBOLIC_AB_BUILD_ONLY=1 \
  bash tools/ci/linux-perf-symbolic-source-bucket-screen-v1.sh "$root" "$corpus" \
    | tee "$root/evidence/symbolic-source-call-safety-build.txt"
grep -Fq 'MINIC_SYMBOLIC_SOURCE_BUCKET_PAIR_READY=PASS' \
  "$root/evidence/symbolic-source-call-safety-build.txt"

make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$root/symbolic-source-call-after" \
  check-ast-contract check-type \
  >"$root/evidence/symbolic-source-call-contracts.txt" 2>&1 || {
    tail -n 100 "$root/evidence/symbolic-source-call-contracts.txt"; exit 21;
  }

# Build SANITIZED MiniC source that differs solely by instrumentation.
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  make -j4 MODE=sanitize CFLAGS=-Werror \
  BUILD_DIR="$root/symbolic-source-call-sanitized" \
  "$root/symbolic-source-call-sanitized/bin/minic" \
  "$root/symbolic-source-call-sanitized/bin/minic-cc" \
  check-ast-contract check-type \
  >"$root/evidence/symbolic-source-call-sanitizer-contracts.txt" 2>&1 || {
    tail -n 160 "$root/evidence/symbolic-source-call-sanitizer-contracts.txt"; exit 22;
  }

# Insert TEST-ONLY hooks into a third disposable build. The instrumentation
# cannot leak into the A/B compiler or the future production promotion.
python3 - <<'PY'
from pathlib import Path
p=Path("src/compiler/compiler.c")
source=p.read_text()
start=source.index("static bool minic_specialize_inline_symbolic_calls(\n")
end=source.index("\nstatic bool minic_inline_boolean_domain_value(",start)
block=source[start:end]
def once(before,after,label):
    global block
    if block.count(before)!=1:
        raise SystemExit("MINIC_SYMBOLIC_SAFETY_HOOK=FAIL "+label)
    block=block.replace(before,after,1)
once("""        bool symbolic_owner_index_ready =
            (symbolic_owned_expression_count == 0U ||""",
"""        bool symbolic_owner_index_ready =
            getenv("MINIC_TEST_FORCE_SYMBOLIC_OWNER_FALLBACK") == NULL &&
            (symbolic_owned_expression_count == 0U ||""","owner-oom")
once("""        if (symbolic_owner_index_ready &&
            symbolic_owned_expression_count != 0U &&""",
"""        if (getenv("MINIC_TEST_FORCE_SYMBOLIC_BUCKET_FALLBACK") == NULL &&
            symbolic_owner_index_ready &&
            symbolic_owned_expression_count != 0U &&""","bucket-oom")
once("""        (void)original_function_count;
        for (caller_index = 0U;""",
"""        if (getenv("MINIC_TEST_FORCE_SYMBOLIC_BUCKET_FALLBACK") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_TEST_FORCE_SYMBOLIC_BUCKET_FALLBACK\\n");
        }
        if (getenv("MINIC_TEST_FORCE_SYMBOLIC_OWNER_FALLBACK") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_TEST_FORCE_SYMBOLIC_OWNER_FALLBACK\\n");
        }
        (void)original_function_count;
        for (caller_index = 0U;""","proof")
p.write_text(source[:start]+block+source[end:])
print("MINIC_SYMBOLIC_SAFETY_HOOK=APPLIED fallback_modes=2")
PY
git diff --check
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$root/symbolic-source-call-forced" \
  "$root/symbolic-source-call-forced/bin/minic" \
  "$root/symbolic-source-call-forced/bin/minic-cc" \
  >"$root/evidence/symbolic-source-call-forced-build.txt" 2>&1 || {
    tail -n 160 "$root/evidence/symbolic-source-call-forced-build.txt";exit 23;
  }

python3 tools/ci/linux-perf-symbolic-source-bucket-safety-v1.py \
  --corpus "$corpus" \
  --baseline-driver "$root/symbolic-source-call-before/bin/minic" \
  --candidate-driver "$root/symbolic-source-call-after/bin/minic" \
  --forced-driver "$root/symbolic-source-call-forced/bin/minic" \
  --sanitized-driver "$root/symbolic-source-call-sanitized/bin/minic" \
  --out "$root/symbolic-source-call-safety" \
  | tee "$root/evidence/symbolic-source-call-safety-verdict.txt"
grep -Fq 'MINIC_SYMBOLIC_BUCKET_SAFETY=PASS cases=8 arms=5 model=9000' \
  "$root/evidence/symbolic-source-call-safety-verdict.txt"
echo "MINIC_SYMBOLIC_SOURCE_BUCKET_SAFETY_GATE=PASS production_C_unchanged=true"

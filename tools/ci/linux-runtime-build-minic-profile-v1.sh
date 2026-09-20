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
  apply-inline-specialization-core-empty-function-v0.py
  apply-inline-specialization-label-alias-v0.py
  apply-riscv64-default-text-section-v0.py
  apply-riscv64-explicit-section-flags-v0.py
  apply-inline-asm-symbolic-specialization-v0.py
  apply-riscv64-core-value-slot-pack-v0.py
  apply-riscv64-core-value-slot-reuse-v0.py
  apply-riscv64-pi-local-symbol-address-v0.py
)

start=$(date +%s%N)
: >"$ev/patch.log"
for p in "${patches[@]}"; do
  python3 "tools/ci/$p" >>"$ev/patch.log"
done
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build_dir" \
  "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >/dev/null
end=$(date +%s%N)
echo "TIMING toolchain_ms=$(((end-start)/1000000))" | tee "$ev/toolchain-timing.txt"
printf '%s\n' "${patches[@]}" >"$ev/minic-profile.txt"
sha256sum "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >"$ev/minic.sha256"

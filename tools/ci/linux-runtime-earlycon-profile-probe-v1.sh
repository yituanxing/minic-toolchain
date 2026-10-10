#!/usr/bin/env bash
# Opt-in side-by-side regression: does the runtime 39-patch MiniC profile
# change earlycon CFG compared with the SAME-COMMIT unpatched compiler?
# Runs in the existing focused T1 job, never a new Workflow or branch.
set -Eeuo pipefail
: "${BASELINE_MINIC:?}"
: "${PROBE_DIR:?}"
: "${GITHUB_SHA:?}"
: "${GH_TOKEN:?}"
repo_root=$(git rev-parse --show-toplevel)
mkdir -p "$PROBE_DIR"
abs_probe=$(realpath "$PROBE_DIR")
temp_worktree="${RUNNER_TEMP:-/tmp}/minic-earlycon-probe-${GITHUB_RUN_ID:-$$}"
cleanup() {
  git -C "$repo_root" worktree remove --force "$temp_worktree" 2>/dev/null || true
}
trap cleanup EXIT
git -C "$repo_root" worktree add --detach "$temp_worktree" "$GITHUB_SHA"
# The Linux runtime candidate profile modifies only the TEMP worktree.
(
  cd "$temp_worktree"
  bash tools/ci/linux-runtime-build-minic-profile-v1.sh \
    "$abs_probe/profile-build" "$abs_probe/profile-evidence" \
      >"$abs_probe/profile-build.log" 2>&1
)
profile_minic="$abs_probe/profile-build/bin/minic"
test -x "$profile_minic"
printf 'EARLYCON_PROFILE_DRIVER_HASH baseline=%s runtime=%s\n' \
  "$(sha256sum "$BASELINE_MINIC" | cut -d' ' -f1)" \
  "$(sha256sum "$profile_minic" | cut -d' ' -f1)"
# minic is only the lightweight driver, the semantically relevant binary is
# adjacent minic-cc. Driver SHA equality says nothing about Core Lowering.
printf 'EARLYCON_PROFILE_COMPILER_HASH baseline=%s runtime=%s\n' \
  "$(sha256sum "$(dirname "$BASELINE_MINIC")/minic-cc" | cut -d' ' -f1)" \
  "$(sha256sum "$(dirname "$profile_minic")/minic-cc" | cut -d' ' -f1)"
# These exact two probes used to pass under unpatched MiniC. Run them again
# under the production runtime patch stack; a failing result localizes the
# cause to the patch stack without touching any certified 2064 .o inputs.
profile_test_rc=0
(
  cd "$temp_worktree"
  MINIC="$profile_minic" BUILD_DIR="$abs_probe/rv64" \
  RISCV_CC=riscv64-linux-gnu-gcc RISCV_LD=riscv64-linux-gnu-ld \
  QEMU_RISCV64=qemu-riscv64 \
    bash tests/compiler/c0/run-earlycon-two-pass-rv64.sh
) >"$abs_probe/profile-two-pass.log" 2>&1 || profile_test_rc=$?
grep -E 'EARLYCON_(TWO_PASS|TABLE)_' "$abs_probe/profile-two-pass.log" || true
echo "EARLYCON_PROFILE_MINIMAL_RESULT rc=$profile_test_rc"
# Isolate the first semantic patch in a SEPARATE temporary detached worktree.
# Its compiler identity and proof are independent from both baseline and the
# full 39-patch profile. If this first patch reproduces the bug we can fix a
# single small local-constant propagation transfer rule immediately.
first_tree="${RUNNER_TEMP:-/tmp}/minic-earlycon-first-${GITHUB_RUN_ID:-$}"
git -C "$repo_root" worktree add --detach "$first_tree" "$GITHUB_SHA"
(
  cd "$first_tree"
  python3 tools/ci/apply-local-integer-assignment-v2.py
  make -j4 MODE=release CFLAGS=-Werror \
     BUILD_DIR="$abs_probe/first-patch-build" \
     "$abs_probe/first-patch-build/bin/minic" \
     "$abs_probe/first-patch-build/bin/minic-cc"
) >"$abs_probe/first-patch-build.log" 2>&1
first_test_rc=0
(
  cd "$first_tree"
  MINIC="$abs_probe/first-patch-build/bin/minic" \
   BUILD_DIR="$abs_probe/first-patch-rv64" \
   RISCV_CC=riscv64-linux-gnu-gcc RISCV_LD=riscv64-linux-gnu-ld \
   QEMU_RISCV64=qemu-riscv64 \
   bash tests/compiler/c0/run-earlycon-two-pass-rv64.sh
) >"$abs_probe/first-patch-two-pass.log" 2>&1 || first_test_rc=$?
echo "EARLYCON_FIRST_PATCH_TEST rc=$first_test_rc patch=apply-local-integer-assignment-v2"
grep -E 'EARLYCON_(TWO_PASS|TABLE)_' "$abs_probe/first-patch-two-pass.log" || true
echo "EARLYCON_FIRST_PATCH_CC_SHA256=$(sha256sum "$abs_probe/first-patch-build/bin/minic-cc" | cut -d' ' -f1)"
# Find the earliest runnable bad prefix in ONE CI job. The exact
# production patch order is read from the canonical runtime profile.
# Reuse one checkout and incremental objects; do not launch 39 workflows.
prefix_n=1
first_bad=0
mapfile -t patch_sequence < <(
  sed -n '/^patches=(/,/^)/p' "$first_tree/tools/ci/linux-runtime-build-minic-profile-v1.sh" |
    awk '/^[[:space:]]+apply-/ {print $1}'
)
if ((${#patch_sequence[@]}<30)) || [[ "${patch_sequence[0]}" != "apply-local-integer-assignment-v2.py" ]]; then
  echo "EARLYCON_PATCH_PREFIX=ERROR bad_manifest count=${#patch_sequence[@]}"
  exit 9
fi
for ((pn=1;pn<${#patch_sequence[@]};pn++)); do
  patch=${patch_sequence[pn]}
  ((prefix_n += 1))
  if [[ "$patch" == "apply-inline-asm-symbolic-specialization-v0.py" ]]; then
    # The nominal 32nd patch RUNS SEVEN hidden follow-up patches inside
    # exec(). Isolate those seven one by one; the earlier 31 prefixes have
    # passed real QEMU. No full-kernel rebuild is ever needed here.
    echo "EARLYCON_PATCH_STACK_UNPACK=START at_profile_index=$prefix_n"
    (
      cd "$first_tree"
      python3 - <<'PY_UNPACK_PATCH'
from pathlib import Path
path = Path("tools/ci/apply-inline-asm-symbolic-specialization-v0.py")
src = path.read_text()
anchor = "# Keep expanded final-link validation on the exact focused semantic stack."
assert src.count(anchor) == 1
main = src.split(anchor, 1)[0]
exec(compile(main, str(path)+" [main only]", "exec"))
PY_UNPACK_PATCH
    ) >>"$abs_probe/prefix-patches.log" 2>&1
    chain=(apply-local-null-pointer-conditions-v0.py
           apply-inline-specialization-boolean-range-v1.py
           apply-inline-specialization-local-boolean-domain-v0.py
           apply-constant-inline-call-cfg-v1.py
           apply-local-integer-arithmetic-closure-v0.py
           apply-constant-cfg-product-v0.py
           apply-core-unreachable-reentry-v0.py)
    for ((sub=-1;sub<${#chain[@]};sub++)); do
      if ((sub>=0)); then
        (
          cd "$first_tree"
          python3 "tools/ci/${chain[sub]}"
        ) >>"$abs_probe/prefix-patches.log" 2>&1
      fi
      phase=main_only
      if ((sub>=0)); then phase=${chain[sub]}; fi
      if (
        cd "$first_tree"
        make -j4 MODE=release CFLAGS=-Werror \
          BUILD_DIR="$abs_probe/first-patch-build" \
          "$abs_probe/first-patch-build/bin/minic" \
          "$abs_probe/first-patch-build/bin/minic-cc"
      ) >"$abs_probe/chain-build-${sub}.log" 2>&1; then
        sub_rc=0
        (
          cd "$first_tree"
          MINIC="$abs_probe/first-patch-build/bin/minic" \
            BUILD_DIR="$abs_probe/chain-rv64-${sub}" \
            RISCV_CC=riscv64-linux-gnu-gcc RISCV_LD=riscv64-linux-gnu-ld \
            QEMU_RISCV64=qemu-riscv64 \
            bash tests/compiler/c0/run-earlycon-two-pass-rv64.sh
        ) >"$abs_probe/chain-test-${sub}.log" 2>&1 || sub_rc=$?
        echo "EARLYCON_PATCH_CHAIN stage=${sub} name=$phase build=PASS rv64_rc=$sub_rc"
        grep -E 'EARLYCON_(TWO_PASS|TABLE)_' "$abs_probe/chain-test-${sub}.log" || true
        if ((sub_rc!=0)); then
          first_bad=32
          echo "EARLYCON_PATCH_FIRST_BAD_SUBPATCH stage=$sub name=$phase"
          break
        fi
      else
        echo "EARLYCON_PATCH_CHAIN stage=$sub name=$phase build=UNAVAILABLE"
        tail -n 3 "$abs_probe/chain-build-${sub}.log"
      fi
    done
    break
  fi
  ( cd "$first_tree" && python3 "tools/ci/$patch" ) >>"$abs_probe/prefix-patches.log" 2>&1
  if (
    cd "$first_tree"
    make -j4 MODE=release CFLAGS=-Werror \
      BUILD_DIR="$abs_probe/first-patch-build" \
      "$abs_probe/first-patch-build/bin/minic" \
      "$abs_probe/first-patch-build/bin/minic-cc"
  ) >"$abs_probe/prefix-build-$prefix_n.log" 2>&1; then
    prefix_rc=0
    (
      cd "$first_tree"
      MINIC="$abs_probe/first-patch-build/bin/minic" \
        BUILD_DIR="$abs_probe/prefix-rv64-$prefix_n" \
        RISCV_CC=riscv64-linux-gnu-gcc RISCV_LD=riscv64-linux-gnu-ld \
        QEMU_RISCV64=qemu-riscv64 \
        bash tests/compiler/c0/run-earlycon-two-pass-rv64.sh
    ) >"$abs_probe/prefix-test-$prefix_n.log" 2>&1 || prefix_rc=$?
    echo "EARLYCON_PATCH_PREFIX index=$prefix_n patch=$patch build=PASS rv64_rc=$prefix_rc"
    grep -E 'EARLYCON_(TWO_PASS|TABLE)_' "$abs_probe/prefix-test-$prefix_n.log" || true
    if ((prefix_rc!=0)); then
      first_bad=$prefix_n
      echo "EARLYCON_PATCH_FIRST_BAD buildable_prefix=$prefix_n patch=$patch"
      break
    fi
  else
    echo "EARLYCON_PATCH_PREFIX index=$prefix_n patch=$patch build=UNAVAILABLE"
    tail -n 3 "$abs_probe/prefix-build-$prefix_n.log"
  fi
done
if ((first_bad==0)); then
  echo "EARLYCON_PATCH_FIRST_BAD=NOT_FOUND_IN_PREFIX_TEST last_prefix=$prefix_n"
fi
git -C "$repo_root" worktree remove --force "$first_tree"


# The precise source is already in a certified October 10 receiver artifact.
# It is read-only, pinned by real GNU-E SHA below, and ONLY used as an input to
# compare compiler-generated source-level control flow. Never upload or cache
# it as a golden object or pretend it is a boot oracle.
artifact_run=38046231186
artifact_name="sharded-gnu-minic-QEMU-8075e40ce8c5c1da10a63ad44fa92891fafd76e2"
gh run download "$artifact_run" \
   -R yituanxing/minic-toolchain -n "$artifact_name" -D "$abs_probe/pinned"
input="$abs_probe/pinned/trials/full_all/earlycon-source/earlycon-exact.i"
test -s "$input"
expected=bd9dd3c9e390690ebb6d0f943b4246f9b25919b7dd4e539adda779d3c84be122
observed=$(sha256sum "$input" | cut -d' ' -f1)
[[ "$observed" == "$expected" ]] || {
  echo "EARLYCON_PROFILE_INPUT=INCONCLUSIVE digest_changed=$observed"; exit 9;
}
echo "EARLYCON_PROFILE_INPUT=PINNED sha256=$observed"
for variant in baseline runtime; do
  if [[ "$variant" == baseline ]]; then compiler="$BASELINE_MINIC"; else compiler="$profile_minic"; fi
  started=$(date +%s%N)
  compile_rc=0
  "$compiler" -S "$input" -o "$abs_probe/$variant.s" \
     >"$abs_probe/$variant.out" 2>"$abs_probe/$variant.err" || compile_rc=$?
  ended=$(date +%s%N)
  echo "EARLYCON_PROFILE_VARIANT name=$variant rc=$compile_rc elapsed_ms=$(((ended-started)/1000000))"
  if ((compile_rc)); then
    tail -n 15 "$abs_probe/$variant.err"
  fi
done
python3 - "$abs_probe" <<'PY_ANALYZE'
from pathlib import Path
import hashlib,re,sys
p=Path(sys.argv[1])
for kind in ("baseline","runtime"):
    f=p/(kind+".s")
    if not f.is_file() or f.stat().st_size==0:
        print(f"EARLYCON_PROFILE_CROSSCHECK=UNAVAILABLE kind={kind}")
        continue
    asm=f.read_text(errors="replace")
    start=asm.find("\nsetup_earlycon:\n")
    if start<0:
        print(f"EARLYCON_PROFILE_CROSSCHECK=NO_SETUP_SYMBOL kind={kind}")
        continue
    end=asm.find(".size setup_earlycon,",start)
    setup=asm[start:end] if end>=0 else asm[start:start+40000]
    bb=re.search(r"(?m)^\.Lsetup_earlycon_core_bb10:\s*$",setup)
    branch=setup[bb.end():bb.end()+800] if bb else ""
    (p/(kind+"-setup-earlycon.s")).write_text(setup)
    print("EARLYCON_PROFILE_CFG kind=%s setup_bytes=%d bb10=%s "
          "conditional_branches=%d return_negative_two=%s sha256=%s" %
          (kind,len(setup),bool(bb),
           len(re.findall(r"\b(?:beqz|bnez|beq|bne|blt|bge|bltu|bgeu)\b",setup)),
           "-2" in setup,
           hashlib.sha256(setup.encode()).hexdigest()))
    if bb:
        print(f"EARLYCON_PROFILE_BB10 kind={kind} first_180={branch[:180]!r}")
PY_ANALYZE
echo "EARLYCON_PROFILE_PROBE=FINISHED runtime_test_rc=$profile_test_rc exact_i_hash=$observed"

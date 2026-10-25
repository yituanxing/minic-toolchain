#!/usr/bin/env bash
# Batch isolate *real* RV32 Linux runtime faults from seven certified MiniC
# object artifacts.  Original GNU Kbuild, original config, no C recompilation.
set -Eeuo pipefail

: "${RV32I_TRIAGE_ROOT:?}"
: "${RV32I_TRIAGE_SOURCE:?}"
: "${RV32I_TRIAGE_GOLDEN:?}"
if [[ -n "${RV32I_TRIAGE_SHARD:-}" ]]; then
  [[ "$RV32I_TRIAGE_SHARD" =~ ^[0-6]$ ]] || {
    echo 'RV32I_COHORT=FAIL invalid_shard_partition' >&2; exit 2;
  }
fi
if [[ -n "${RV32I_TRIAGE_PARENT_PART:-}" ]]; then
  [[ -n "${RV32I_TRIAGE_SHARD:-}" && "$RV32I_TRIAGE_PARENT_PART" =~ ^[0-7]$ ]] || {
    echo 'RV32I_COHORT=FAIL invalid_parent_partition' >&2; exit 2;
  }
fi

root=$(realpath "$RV32I_TRIAGE_ROOT")
src=$(realpath "$RV32I_TRIAGE_SOURCE")
golden=$(realpath "$RV32I_TRIAGE_GOLDEN")
out="$golden/out"
prov="$golden/provenance"
shards="$root/shards"
mkdir -p "$root/backup" "$root/trials" "$root/runtime"
summary="$root/cohort-results.tsv"
printf 'group\tobjects\tlink\tqemu_p0\timage_sha256\tfirst_fault\n' >"$summary"

test -f "$src/Makefile"
test -s "$out/arch/riscv/boot/Image"
test -s "$prov/full-c-objects.txt"
grep -Fxq 'elf_class=ELF32' "$prov/gcc-baseline.txt"
grep -Fxq 'isa=rv32i' "$prov/gcc-baseline.txt"
grep -Fxq 'abi=ilp32' "$prov/gcc-baseline.txt"
grep -Fxq "config_sha256=$(sha256sum "$out/.config" | cut -d' ' -f1)" "$prov/gcc-baseline.txt"
test "$(sha256sum "$out/.config" | cut -d' ' -f1)" = \
  4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c

# Verify all 1741 owners against the exact seven shard inventories before
# attempting one link.  Never infer membership from artifact ZIP ordering.
python3 - "$prov/full-c-objects.txt" "$shards" "$out" "${RV32I_TRIAGE_SHARD:-all}" <<'PY_AUDIT'
from pathlib import Path
import hashlib, re, sys
listing, root, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
owners = [x for x in listing.read_text().splitlines() if x and not x.startswith('#')]
assert len(owners) == 1741 and len(set(owners)) == len(owners), len(owners)
selected = range(7) if sys.argv[4] == 'all' else [int(sys.argv[4])]
for idx in selected:
    shard = root / f'shard-{idx}'
    names = [x for x in (shard / 'full-objects.txt').read_text().splitlines()
             if x and not x.startswith('#')]
    assert names == owners[idx::7], (idx, len(names))
    manifest = {}
    for line in (shard / 'minic-manifest.sha256').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  minic/(.+\.o)', line)
        assert match, (idx, line[:120])
        manifest[match[2]] = match[1]
    assert set(manifest) == set(names), (idx, len(manifest), len(names))
    for name in names:
        candidate = shard / 'minic' / name
        assert candidate.is_file() and (out/name).is_file(), name
        assert hashlib.sha256(candidate.read_bytes()).hexdigest() == manifest[name], name
    print(f'RV32I_COHORT_AUDIT=PASS shard={idx} objects={len(names)} sha=true original_owner=true')
print(f'RV32I_COHORT_AUDIT=PASS original_owners=1741 selected_shards={len(selected)} duplicate=0')
PY_AUDIT

# Freeze pristine GNU C owners.  Make-generated headers and linking products
# remain owned by original Kbuild; no custom archive order or synthetic source.
mapfile -t owners < <(sed '/^#/d;/^$/d' "$prov/full-c-objects.txt")
for owner in "${owners[@]}"; do
  mkdir -p "$root/backup/$(dirname "$owner")"
  cp --reflink=auto "$out/$owner" "$root/backup/$owner"
done

makeargs=(-C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- KBUILD_NOCMDDEP=1)
# Generated dependency closure must be settled BEFORE installing any MiniC C.
make "${makeargs[@]}" -j4 prepare >"$root/gnu-prepare.log" 2>&1 || {
  tail -n 80 "$root/gnu-prepare.log" >&2; exit 4;
}
make "${makeargs[@]}" -j4 Image >"$root/gnu-reconcile.log" 2>&1 || {
  tail -n 80 "$root/gnu-reconcile.log" >&2; exit 4;
}
echo 'RV32I_COHORT_PREPARE=PASS original_GNU_Kbuild no_MiniC_objects_yet=true'
# Generated headers may legitimately rebuild GNU owners: record the final
# settled original GNU objects, not a stale earlier snapshot.
for owner in "${owners[@]}"; do
  cp --reflink=auto "$out/$owner" "$root/backup/$owner"
done

# Identical pinned software-arithmetic seam used in the full MiniC Image.
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$root/runtime" \
  >"$root/runtime-build.log" 2>&1 || {
    tail -n 80 "$root/runtime-build.log" >&2; exit 5;
  }
mapfile -t runtime_objects <"$root/runtime/rv32-runtime-objects.rsp"
riscv64-linux-gnu-ar rcsD "$root/runtime/rv32i-softarith.a" "${runtime_objects[@]}"
lib="$root/runtime/rv32i-softarith.a"
echo "RV32I_COHORT_RUNTIME=PASS sha=$(sha256sum "$lib" | cut -d' ' -f1)"

# CRITICAL DIFFERENTIAL CONTROL: six disjoint MiniC object groups previously
# produced the same early stack-end panic.  Before blaming six unrelated C
# owners, prove original GNU objects STILL boot after the very same Kbuild
# --eval softarith library seam and relink path used by each mixed trial.
control="$root/trials/gnu-relink-control"
mkdir -p "$control"
touch "$out/init/main.o"
if ! make "${makeargs[@]}" -j4 \
    --eval="vmlinux: KBUILD_VMLINUX_LIBS += $lib" \
    Image >"$control/link.log" 2>&1; then
  echo 'RV32I_GNU_RELINK_CONTROL=FAIL original_GNU_link_seam' >&2
  tail -n 80 "$control/link.log" >&2
  exit 10
fi
if [[ "$(sha256sum "$out/init/main.o" | cut -d' ' -f1)" != \
      "$(sha256sum "$root/backup/init/main.o" | cut -d' ' -f1)" ]]; then
  echo 'RV32I_GNU_RELINK_CONTROL=FAIL original_GNU_C_changed' >&2
  exit 10
fi
if RV32I_QEMU_IMAGE="$out/arch/riscv/boot/Image" \
    RV32I_QEMU_WORK="$control/qemu" RV32I_QEMU_TIMEOUT_SECONDS=20 \
    RV32I_QEMU_SKIP_REPLAY=1 \
    RV32I_QEMU_EARLYCON=uart8250,mmio,0x10000000,115200n8 \
    bash tools/ci/linux-rv32i-qemu-p0-v1.sh >"$control/qemu-step.log" 2>&1; then
  echo "RV32I_GNU_RELINK_CONTROL=PASS same_Kbuild_seam=true sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)"
else
  echo 'RV32I_GNU_RELINK_CONTROL=FAIL same_Kbuild_seam_runtime_failed; MiniC_cohort_verdicts_NOT_TRUSTED=true' >&2
  grep -E 'Linux version|Kernel panic|Oops:|RV32I_QEMU_P0=|RV32I_LINUX_PID1=' \
    "$control/qemu-step.log" | tail -n 30 >&2 || true
  tail -n 45 "$control/qemu-step.log" >&2 || true
  exit 10
fi

mkdir -p "$root/subsets"
if [[ -n "${RV32I_TRIAGE_SHARD:-}" ]]; then
  # Fixed stride within one certified 249/248-owner producer shard. Seven
  # independent runners each perform eight ~31-object QEMU trials in parallel.
  python3 - "$shards/shard-$RV32I_TRIAGE_SHARD/full-objects.txt" "$root/subsets" "$RV32I_TRIAGE_SHARD" "${RV32I_TRIAGE_PARENT_PART:-all}" <<'PY_SPLIT'
from pathlib import Path
import sys
src, dest, idx = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
names = [x for x in src.read_text().splitlines() if x and not x.startswith('#')]
assert len(names) in (248, 249) and len(set(names)) == len(names)
if sys.argv[4] != 'all':
    parent = int(sys.argv[4])
    assert 0 <= parent < 8
    names = names[parent::8]
    assert len(names) in (31, 32)
for part in range(8):
    subset = names[part::8]
    assert subset
    label = (f'shard-{idx}-part-{part}' if sys.argv[4] == 'all' else
             f'shard-{idx}-part-{parent}-leaf-{part}')
    (dest / f'{label}.txt').write_text('\n'.join(subset)+'\n')
    print(f'RV32I_SUBSHARD_PLAN=PASS group={label} objects={len(subset)}')
PY_SPLIT
  mapfile -t trial_files < <(find "$root/subsets" -type f -name '*.txt' | LC_ALL=C sort)
else
  trial_files=()
  for idx in 0 1 2 3 4 5 6; do
    trial_files+=("$shards/shard-$idx/full-objects.txt")
  done
fi
previous_file=
qemu_count=0
for active_file in "${trial_files[@]}"; do
  active_name=$(basename "$active_file" .txt)
  if [[ "$active_name" == full-objects ]]; then
    active_name=$(basename "$(dirname "$active_file")")
  fi
  idx=$(sed -n 's/^shard-\([0-6]\).*/\1/p' <<<"$active_name")
  [[ "$idx" =~ ^[0-6]$ ]]
  group="$root/trials/$active_name"
  mkdir -p "$group"
  if [[ -n "$previous_file" ]]; then
    while IFS= read -r owner; do
      [[ "$owner" == \#* || -z "$owner" ]] && continue
      cp --reflink=auto "$root/backup/$owner" "$out/$owner"
      touch "$out/$owner"
    done <"$previous_file"
  fi
  count=0
  while IFS= read -r owner; do
    [[ "$owner" == \#* || -z "$owner" ]] && continue
    cp --reflink=auto "$shards/shard-$idx/minic/$owner" "$out/$owner"
    touch "$out/$owner"
    count=$((count+1))
  done <"$active_file"
  previous_file=$active_file
  echo "RV32I_COHORT=START group=$active_name objects=$count original_GNU_Kbuild=true"
  if ! make "${makeargs[@]}" -j4 \
      --eval="vmlinux: KBUILD_VMLINUX_LIBS += $lib" \
      Image >"$group/link.log" 2>&1; then
    echo -e "$active_name\t$count\tFAIL\tNOT_RUN\t-\tlink_error" >>"$summary"
    tail -n 45 "$group/link.log" >&2
    continue
  fi
  # Kbuild sometimes recompiles a changed generated-header dependent owner.
  # Never call such a trial a MiniC-vs-GNU comparison without exact SHA.
  if ! python3 - "$shards/shard-$idx" "$out" "$active_file" <<'PY_VERIFY'
from pathlib import Path
import hashlib, sys
shard, out, active = map(Path, sys.argv[1:])
names = set(x for x in active.read_text().splitlines() if x and not x.startswith('#'))
seen = set()
for line in (shard/'minic-manifest.sha256').read_text().splitlines():
    expected, path = line.split('  ', 1)
    name = path.removeprefix('minic/')
    if name not in names:
        continue
    seen.add(name)
    if hashlib.sha256((out/name).read_bytes()).hexdigest() != expected:
        raise SystemExit(f'RV32I_COHORT_DRIFT=FAIL owner={name}')
assert seen == names, (len(seen), len(names))
print('RV32I_COHORT_DRIFT=PASS exact_MiniC_cohort=true')
PY_VERIFY
  then
    echo -e "$active_name\t$count\tDRIFT\tNOT_RUN\t-\tKbuild_recompiled_candidate" >>"$summary"
    continue
  fi
  image="$out/arch/riscv/boot/Image"
  digest=$(sha256sum "$image" | cut -d' ' -f1)
  if RV32I_QEMU_IMAGE="$image" RV32I_QEMU_WORK="$group/qemu" \
      RV32I_QEMU_TIMEOUT_SECONDS=20 RV32I_QEMU_SKIP_REPLAY=1 \
      RV32I_QEMU_EARLYCON=uart8250,mmio,0x10000000,115200n8 \
      bash tools/ci/linux-rv32i-qemu-p0-v1.sh >"$group/qemu-step.log" 2>&1; then
    result=PASS
  else
    result=FAIL
  fi
  qemu_count=$((qemu_count+1))
  fault=$(grep -m1 -E 'Kernel panic|Oops:|Unable to handle|RV32I_LINUX_PID1=' \
    "$group/qemu/qemu-serial.log" 2>/dev/null | sed 's/^[^]]*] //' || true)
  fault=${fault:-no_kernel_marker}
  fault=${fault//$'\t'/ }
  echo -e "$active_name\t$count\tPASS\t$result\t$digest\t$fault" >>"$summary"
  echo "RV32I_COHORT=RESULT group=$active_name objects=$count qemu_p0=$result image_sha256=$digest first_fault=$fault"
  if [[ "$result" == FAIL ]]; then
    echo "RV32I_COHORT_DIAG=BEGIN group=$active_name"
    grep -E 'Linux version|Kernel panic|Oops:|RV32I_QEMU_P0=FAIL|RV32I_LINUX_PID1=|Unable to handle' \
      "$group/qemu-step.log" | tail -n 12 || true
    tail -n 12 "$group/qemu-step.log" || true
    echo "RV32I_COHORT_DIAG=END group=$active_name"
  fi
done

echo 'RV32I_COHORT_BATCH=RESULTS'
column -t -s $'\t' "$summary" || sed -n '1,20p' "$summary"
test "$qemu_count" -gt 0 || { echo 'RV32I_COHORT_BATCH=FAIL zero_actual_QEMU_trials'; exit 9; }
echo "RV32I_COHORT_BATCH=PASS actual_QEMU_trials=$qemu_count exact_GNU_plus_MiniC_cohorts=true Linux_C_recompiles=0"

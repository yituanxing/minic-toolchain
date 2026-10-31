#!/usr/bin/env bash
# RV32I entry into the ALREADY established Linux 6.6.143 Kbuild acceptance.
# Golden mode freezes the original GNU RV32 Kbuild tree. Shard mode runs the
# existing seven-way owner/manifest contract over the REAL ELF32 C universe.
# The historical five-owner entry remains diagnostic-only.
set -Eeuo pipefail
mode=${RV32I_LINUX_MODE:-entry}
case "$mode" in config|golden|shard|union|entry) ;; *) echo "RV32I_LINUX=FAIL invalid_mode=$mode" >&2; exit 2 ;; esac
if [[ "$mode" == entry || "$mode" == shard || "$mode" == union ]]; then
  : "${MINIC_CC:?required current-main MiniC}"
fi
: "${RV32I_LINUX_SOURCE:?required pinned original 6.6.143 source cache}"
: "${RV32I_LINUX_WORK:?required isolated evidence dir}"
src=$(realpath "$RV32I_LINUX_SOURCE")
test -f "$src/Makefile"
if [[ "$mode" == entry || "$mode" == shard || "$mode" == union ]]; then test -x "$MINIC_CC"; fi
mkdir -p "$RV32I_LINUX_WORK"
out="$RV32I_LINUX_WORK/out"
ev="$RV32I_LINUX_WORK/evidence"
prov="$RV32I_LINUX_WORK/provenance"
mkdir -p "$out" "$ev" "$prov"
cd "$RV32I_LINUX_WORK"
export LC_ALL=C
cross=riscv64-linux-gnu-
gnu_args=(-C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross")
tools=(make riscv64-linux-gnu-gcc riscv64-linux-gnu-as riscv64-linux-gnu-ld riscv64-linux-gnu-readelf sha256sum)
for tool in "${tools[@]}"; do command -v "$tool" >/dev/null; done
if [[ "$mode" == shard ]]; then
  : "${RV32I_SHARD:?required 0..6 shard index}"
  [[ "$RV32I_SHARD" =~ ^[0-6]$ ]] || {
    echo "RV32I_SHARD=FAIL invalid_index=$RV32I_SHARD" >&2; exit 2;
  }
  test -s "$prov/gcc-baseline.txt"
  test -s "$prov/gcc-objects.sha256"
  test -s "$prov/full-c-objects.txt"
  test -s "$out/arch/riscv/boot/Image"
  test -s "$out/.config"
  cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
  grep -Fxq "config_sha256=$cfg" "$prov/gcc-baseline.txt"
  grep -Fxq "elf_class=ELF32" "$prov/gcc-baseline.txt"
  grep -Fxq "image_sha256=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)" "$prov/gcc-baseline.txt"
  (cd "$out" && sha256sum --quiet -c "$prov/gcc-objects.sha256") || {
    echo 'RV32I_SHARD=FAIL GNU_golden_object_drift' >&2; exit 8;
  }
  shard="$RV32I_LINUX_WORK/shard-$RV32I_SHARD"
  mkdir -p "$shard/minic"
  python3 "$GITHUB_WORKSPACE/tools/ci/linux-runtime-gcc-shards-v1.py" split \
    --full "$prov/full-c-objects.txt" --shard "$RV32I_SHARD" --count 7 \
    --output "$shard/full-objects.txt"
  mapfile -t owners < <(sed '/^#/d;/^$/d' "$shard/full-objects.txt")
  compiler_sha=$(sha256sum "$MINIC_CC" | cut -d' ' -f1)
  assembler_sha=$(sha256sum "$(command -v riscv64-linux-gnu-as)" | cut -d' ' -f1)
  identity=$(printf '%s\n' "$cfg" \
    "$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)" \
    "$compiler_sha" "$assembler_sha" \
    "$(sha256sum "$prov/gcc-objects.sha256" | cut -d' ' -f1)" \
    "$(sha256sum "$shard/full-objects.txt" | cut -d' ' -f1)" | sha256sum | cut -d' ' -f1)
  if [[ -s "$shard/minic-identity.txt" && -s "$shard/minic-manifest.sha256" ]]; then
    test "$(cat "$shard/minic-identity.txt")" = "$identity"
    (cd "$shard" && sha256sum --quiet -c minic-manifest.sha256)
    test "$(wc -l <"$shard/minic-manifest.sha256")" -eq "${#owners[@]}"
    echo "RV32I_SHARD_CACHE=HIT shard=$RV32I_SHARD objects=${#owners[@]}"
    exit 0
  fi
  : >"$shard/mini-success-objects.txt"
  : >"$shard/mini-object-hashes.sha256"
  : >"$shard/compile.log"
  # The restored GNU out/ tree is on a different runner. Complete generated
  # headers and RV32 vDSO prerequisites with original GNU tools before
  # evicting selected C owners. Otherwise Kbuild can rebuild hwprobe.o as an
  # unselected side effect while CC points at MiniC and corrupt shard scope.
  if ! make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j4 prepare vdso_prepare \
      >"$shard/gnu-prepare.log" 2>&1; then
    echo 'RV32I_SHARD_GNU_PREPARE=FAIL' >&2
    tail -n 60 "$shard/gnu-prepare.log" >&2
    exit 9
  fi
  (cd "$out" && sha256sum --quiet -c "$prov/gcc-objects.sha256") || {
    echo 'RV32I_SHARD_GNU_PREPARE=FAIL original_object_drift' >&2
    exit 9
  }
  echo "RV32I_SHARD_GNU_PREPARE=PASS shard=$RV32I_SHARD golden_object_hashes=unchanged"
  compile_rc=0
  for owner in "${owners[@]}"; do
    test -s "$out/$owner"
    # Force Kbuild to route the selected C owner through MiniC, while GNU
    # remains authoritative for every other part of the golden tree.
    rm -f -- "$out/$owner"
  done
  MINIC="$MINIC_CC" REAL_CC=riscv64-linux-gnu-gcc \
    MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 MINIC_PRESERVE_FAILURE_INPUTS=1 \
    MINIC_KBUILD_SUCCESS_TRACE="$shard/mini-success-objects.txt" \
    MINIC_KBUILD_OBJECT_HASH_TRACE="$shard/mini-object-hashes.sha256" \
    make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j6 -k CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" \
      "${owners[@]}" >"$shard/compile.log" 2>&1 || compile_rc=$?
  : >"$shard/compile-blockers.txt"
  for owner in "${owners[@]}"; do
    if [[ -s "$out/$owner" ]]; then
      mkdir -p "$shard/minic/$(dirname "$owner")"
      cp -a "$out/$owner" "$shard/minic/$owner"
    else
      printf '%s\n' "$owner" >>"$shard/compile-blockers.txt"
    fi
  done
  failed=$(wc -l <"$shard/compile-blockers.txt")
  echo "RV32I_SHARD_SWEEP shard=$RV32I_SHARD attempted=${#owners[@]} failed=$failed make_rc=$compile_rc"
  if ((failed != 0 || compile_rc != 0)); then
    # Preserve the WHOLE -k frontier, including GNU dependency errors which
    # have no MiniC .failed.i, rather than fixing only the first log tail.
    grep -nEi 'fatal error:|error:|Error [0-9]+|undefined reference|recipe for target' \
      "$shard/compile.log" >"$shard/make-diagnostics.txt" || true
    printf 'object\tstate\tbytes\tsha256\n' >"$shard/failure-pool-index.tsv"
    kept=0
    kept_bytes=0
    while IFS= read -r owner; do
      frozen="$out/${owner%.o}.minic-stage2.failed.i"
      stderr="$out/${owner%.o}.minic-stage2.failed.stderr"
      if [[ -s "$frozen" ]]; then
        bytes=$(stat -c %s "$frozen")
        digest=$(sha256sum "$frozen" | cut -d' ' -f1)
        if ((kept < 32 && kept_bytes + bytes <= 128 * 1024 * 1024)); then
          path="$shard/failures/${owner%.o}"
          mkdir -p "$(dirname "$path")"
          cp -a "$frozen" "$path.failed.i"
          [[ ! -f "$stderr" ]] || cp -a "$stderr" "$path.failed.stderr"
          ((kept+=1))
          ((kept_bytes+=bytes))
          printf '%s\tKEPT\t%s\t%s\n' "$owner" "$bytes" "$digest" >>"$shard/failure-pool-index.tsv"
        else
          printf '%s\tOMITTED_CAP\t%s\t%s\n' "$owner" "$bytes" "$digest" >>"$shard/failure-pool-index.tsv"
        fi
      else
        printf '%s\tNO_I\t0\t-\n' "$owner" >>"$shard/failure-pool-index.tsv"
      fi
    done <"$shard/compile-blockers.txt"
    python3 - "$shard" <<'PY_FAILURE_GROUPS'
from collections import defaultdict
from pathlib import Path
import re
import sys

shard = Path(sys.argv[1])
classes = defaultdict(list)
for obj in (shard / 'compile-blockers.txt').read_text().splitlines():
    stderr = shard / 'failures' / (obj[:-2] + '.failed.stderr')
    lines = stderr.read_text(errors='replace').splitlines() if stderr.is_file() else []
    error = next((match.group(1).strip() for line in lines
                  if (match := re.search(r'\b(?:fatal )?error\s*:\s*(.*)', line))), '')
    # Group by error class, retain exact stderr per owner for diagnosis.
    key = re.sub(r'\b\d+\b', '<N>', error)[:240] if error else 'unclassified/see-make-diagnostics'
    classes[key].append(obj)
with (shard / 'failure-classes.tsv').open('w') as out:
    out.write('count\tfirst_owner\terror_class\n')
    for error, owners in sorted(classes.items(), key=lambda item: (-len(item[1]), item[0])):
        out.write(f'{len(owners)}\t{owners[0]}\t{error}\n')
print(f'RV32I_SHARD_FAILURE_CLASSES=COLLECTED unique={len(classes)} all_failed={sum(map(len,classes.values()))}')
PY_FAILURE_GROUPS
    echo "RV32I_SHARD_BLOCKERS=COLLECTED shard=$RV32I_SHARD failed=$failed frozen=$kept make_errors=$(wc -l <"$shard/make-diagnostics.txt")"
    exit 5
  fi
  python3 "$GITHUB_WORKSPACE/tools/ci/linux-runtime-minic-route-audit-v1.py" \
    --objects-file "$shard/full-objects.txt" \
    --success-trace "$shard/mini-success-objects.txt" \
    --object-digest-trace "$shard/mini-object-hashes.sha256" --candidates "$out"
  : >"$shard/minic-manifest.sha256"
  for owner in "${owners[@]}"; do
    riscv64-linux-gnu-readelf -h "$shard/minic/$owner" | grep -Eq 'Class:[[:space:]]+ELF32'
    printf '%s  minic/%s\n' "$(sha256sum "$shard/minic/$owner" | cut -d' ' -f1)" "$owner" >>"$shard/minic-manifest.sha256"
  done
  printf '%s\n' "$identity" >"$shard/minic-identity.txt"
  {
    echo "COHORT_COMPILE=PASS objects=${#owners[@]}"
    echo "COHORT_ABI=PASS objects=${#owners[@]}"
    echo "COHORT_PRODUCE_ONLY=PASS objects=${#owners[@]}"
  } >"$shard/cohort.log"
  echo "RV32I_SHARD=PASS shard=$RV32I_SHARD objects=${#owners[@]} ELF32=1 SHA=1"
  exit 0
fi
if [[ "$mode" == union ]]; then
  # The same merge logic and seven immutable shard contracts used by RV64,
  # parameterized for real ELF32 instead of substituting a five-owner smoke.
  test -s "$prov/gcc-baseline.txt"
  test -s "$prov/gcc-objects.sha256"
  test -s "$prov/full-c-objects.txt"
  test -s "$out/arch/riscv/boot/Image"
  grep -Fxq 'elf_class=ELF32' "$prov/gcc-baseline.txt"
  grep -Fxq 'isa=rv32i' "$prov/gcc-baseline.txt"
  grep -Fxq 'abi=ilp32' "$prov/gcc-baseline.txt"
  (cd "$out" && sha256sum --quiet -c "$prov/gcc-objects.sha256")
  merged="$RV32I_LINUX_WORK/merged"
  python3 "$GITHUB_WORKSPACE/tools/ci/linux-runtime-gcc-shards-v1.py" merge \
    --elf-class 32 --full "$prov/full-c-objects.txt" \
    --shards "$RV32I_LINUX_WORK" --evidence "$merged" \
    --out "$out" --provenance "$prov" --minic "$MINIC_CC" \
    --assembler "$(command -v riscv64-linux-gnu-as)"
  (cd "$merged" && sha256sum --quiet -c minic-manifest.sha256)
  # The receiver restores a GNU golden out/ tree from cache. Its generated
  # VDSO offsets and other Kbuild prerequisites can be rebuilt on this fresh
  # runner, invalidating C owners which include those headers. Complete the
  # ORIGINAL GNU prepare graph *before* installing immutable MiniC objects;
  # otherwise a subsequent `make Image` may silently recompile some owners
  # with GNU even when KBUILD_NOCMDDEP disables command-line drift checks.
  if ! make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j4 prepare \
      >"$merged/gnu-prepare.log" 2>&1; then
    echo 'RV32I_UNION_PREPARE=FAIL original_GNU_Kbuild' >&2
    tail -n 100 "$merged/gnu-prepare.log" >&2
    exit 6
  fi
  echo 'RV32I_UNION_PREPARE=PASS original_GNU_Kbuild before_MiniC_object_install=true'
  # Freeze exactly the same GNU .cmd dependency records that drove each
  # certified shard. The first real relink can overwrite .cmd for a C owner;
  # dynamic-header replay must not derive its preprocessor flags from that
  # changed state. Reuse the already established RV64 Kbuild replay oracle.
  python3 - "$out" "$prov/full-c-objects.txt" "$merged/gnu-cmd" <<'PY_FREEZE_GNU_CMDS'
from pathlib import Path
import shutil
import sys
out, listing, dest = map(Path, sys.argv[1:])
owners = [x for x in listing.read_text().splitlines() if x and not x.startswith('#')]
for owner in owners:
    target = Path(owner)
    path = target.parent / ('.' + target.name + '.cmd')
    original = out / path
    if original.is_symlink() or not original.is_file():
        raise SystemExit(f'RV32I_GNU_CMD_FREEZE=FAIL owner={owner}')
    frozen = dest / path
    frozen.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(original, frozen)
print(f'RV32I_GNU_CMD_FREEZE=PASS records={len(owners)}')
PY_FREEZE_GNU_CMDS
  while IFS= read -r line; do
    [[ "$line" != \#* && -n "$line" ]] || continue
    install -m 644 "$merged/minic/$line" "$out/$line"
  done <"$prov/full-c-objects.txt"

  # RV32I deliberately has no hardware M extension. The original Linux
  # kernel is built with rv32ima, but MiniC's I-only C objects therefore
  # reference libgcc's integer division/multiplication entry points. Reuse
  # the project's already pinned, QEMU-validated GNU libgcc 14.2.0 and LLVM
  # compiler-rt 19.1.7 provider sources. Build those once for exact RV32I /
  # ILP32 using GNU, separately from Linux's 1741 MiniC C owners.
  runtime="$merged/rv32i-softarith"
  bash "$GITHUB_WORKSPACE/tests/compiler/c0/build-rv32-gcc-runtime.sh" "$runtime" \
    >"$merged/rv32i-softarith-build.log" 2>&1
  mapfile -t runtime_objects <"$runtime/rv32-runtime-objects.rsp"
  test "${#runtime_objects[@]}" -ge 17
  runtime_archive="$merged/rv32i-softarith.a"
  riscv64-linux-gnu-ar rcsD "$runtime_archive" "${runtime_objects[@]}"
  for provider in __mulsi3 __udivsi3 __umodsi3 __muldi3 __udivdi3 __umoddi3; do
    riscv64-linux-gnu-nm -g --defined-only "$runtime_archive" |
      grep -Eq "[[:space:]]${provider}$" || {
        echo "RV32I_SOFTARITH=FAIL missing_upstream_provider=$provider" >&2
        exit 6
      }
  done
  echo "RV32I_SOFTARITH=PASS providers=GNU_libgcc_14.2.0,LLVM_compiler_rt_19.1.7 exact_isa=rv32i ABI=ilp32 archive_sha256=$(sha256sum "$runtime_archive" | cut -d' ' -f1)"

  regen_guard="$GITHUB_WORKSPACE/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py"
  python3 "$regen_guard" --mode snapshot --out "$out" \
    --evidence "$merged/generated-before.json"

  # Keep the original GNU Kbuild linker, object universe, archive ordering,
  # and Linux make targets. Attach only the external software-arithmetic
  # provider at the existing vmlinux library seam (inside --start-group),
  # without replacing any Kbuild-generated KBUILD_VMLINUX_LIBS value.
  # KBUILD_NOCMDDEP avoids command-line drift, but a real VDSO input change
  # legitimately regenerates vdso-offsets.h and its dependent C TUs. The
  # existing Linux regeneration guard reconciles those exact owners below.
  if ! make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j4 \
      --eval="vmlinux: KBUILD_VMLINUX_LIBS += $runtime_archive" \
      Image >"$merged/minic-relink.log" 2>&1; then
    echo 'RV32I_UNION_LINK=FAIL original_GNU_Kbuild' >&2
    tail -n 100 "$merged/minic-relink.log" >&2
    # Preserve the ORIGINAL GNU Kbuild preprocessor flags for the precise
    # MiniC owners named by the whole-Image linker. This is a diagnostic
    # capture, never a compiler replacement, configuration override, or
    # relink fallback. The existing .o.cmd records are written by Kbuild.
    python3 - "$out" "$merged" <<'PY_RV32_LINK_INPUTS'
from pathlib import Path
import re
import shlex
import subprocess
import sys

out, merged = map(Path, sys.argv[1:])
diagnostics = merged / 'link-inputs'
diagnostics.mkdir(exist_ok=True)
owners = ('mm/memory.o', 'drivers/base/regmap/regcache.o',
          'lib/crypto/blake2s-generic.o')
records = []
for owner in owners:
    obj = out / owner
    cmd_file = obj.parent / ('.' + obj.name + '.cmd')
    if not cmd_file.is_file():
        records.append(f'{owner}: missing {cmd_file}')
        continue
    lines = cmd_file.read_text(errors='replace').splitlines()
    command = None
    for index, line in enumerate(lines):
        if not (line.startswith(f'savedcmd_{owner} :=') or
                line.startswith(f'cmd_{owner} :=')):
            continue
        command = line.partition(':=')[2].strip()
        while command.endswith('\\') and index + 1 < len(lines):
            index += 1
            command = command[:-1] + lines[index].strip()
        break
    if command is None:
        records.append(f'{owner}: Kbuild command not found')
        continue
    argv = shlex.split(command)
    if '-c' not in argv or '-o' not in argv:
        records.append(f'{owner}: Kbuild command is not a C compile')
        continue
    argv[argv.index('-c')] = '-E'
    path = diagnostics / (owner.replace('/', '-')[:-2] + '.i')
    argv[argv.index('-o') + 1] = str(path)
    try:
        result = subprocess.run(argv, cwd=out, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=100, check=False)
        records.append(f'{owner}: preprocessor_rc={result.returncode} '
                       f'bytes={path.stat().st_size if path.exists() else 0} '
                       f'diagnostic={result.stderr[-1200:]}')
    except (OSError, subprocess.TimeoutExpired) as exc:
        records.append(f'{owner}: {type(exc).__name__}: {exc}')
(diagnostics / 'capture-summary.txt').write_text('\n'.join(records) + '\n')
for record in records:
    print('RV32I_LINK_INPUT ' + record[:300])
PY_RV32_LINK_INPUTS
    exit 6
  fi
  # Analyze the complete generated-input delta against all 1741 frozen
  # original GNU .cmd dependency closures. Do not restore a stale MiniC
  # object when a generated header's CONTENT changed: replay that owner
  # through GNU -E -> the SAME MiniC -S -> original GNU AS first.
  python3 "$regen_guard" --mode impact --out "$out" \
    --evidence "$merged/generated-before.json" \
    --golden-out "$merged/gnu-cmd" --selected "$prov/full-c-objects.txt" \
    --affected-file "$merged/header-affected-objects.txt" \
    | tee "$merged/header-impact.log"
  python3 "$regen_guard" --mode snapshot --out "$out" \
    --evidence "$merged/generated-after-first-link.json"
  if [[ -s "$merged/header-affected-objects.txt" ]]; then
    : >"$merged/header-replay-success.txt"
    : >"$merged/header-replay-hashes.sha256"
    MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 \
      python3 "$regen_guard" --mode replay --out "$out" \
        --evidence "$merged/generated-before.json" \
        --golden-out "$merged/gnu-cmd" \
        --selected "$merged/header-affected-objects.txt" \
        --wrapper "$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" \
        --minic "$MINIC_CC" \
        --success-trace "$merged/header-replay-success.txt" \
        --digest-trace "$merged/header-replay-hashes.sha256" \
        >"$merged/header-replay.log" 2>&1 || {
          tail -n 90 "$merged/header-replay.log" >&2
          exit 7
        }
    python3 "$GITHUB_WORKSPACE/tools/ci/linux-runtime-minic-route-audit-v1.py" \
      --objects-file "$merged/header-affected-objects.txt" \
      --success-trace "$merged/header-replay-success.txt" \
      --object-digest-trace "$merged/header-replay-hashes.sha256" \
      --candidates "$out"
  fi

  # Restore ONLY genuine GNU-rebuilt C owners that do NOT depend on changed
  # generated headers; dynamic dependents above are already fresh MiniC.
  # Prove each overwritten owner has a real `CC owner.o` in the link log.
  # Reinstalling all 1741 would touch vdso/hwprobe.o and force VDSOLD to
  # regenerate yet again on every retry (the previous CI failure).
  python3 - "$out" "$merged" <<'PY_RECONCILE'
from pathlib import Path
import hashlib
import os
import re
import shutil
import sys
out, merged = map(Path, sys.argv[1:])
affected = set((merged / 'header-affected-objects.txt').read_text().splitlines())
compiled = set(re.findall(r'^  CC\s+(\S+\.o)\s*$',
                          (merged / 'minic-relink.log').read_text(), re.M))
restored = []
for line in (merged / 'minic-manifest.sha256').read_text().splitlines():
    expected, relative = line.split('  ', 1)
    name = relative.removeprefix('minic/')
    if name in affected:
        continue
    target, candidate = out / name, merged / relative
    if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
        if name not in compiled:
            raise SystemExit(f'RV32I_UNION_RECONCILE=FAIL unexplained_C_owner={name}')
        shutil.copy2(candidate, target)
        os.utime(target, None)
        restored.append(name)
(merged / 'owner-restored.txt').write_text(''.join(x + '\n' for x in restored))
print(f'RV32I_UNION_RECONCILE=PASS replay={len(affected)} restored={len(restored)} owners={len(compiled)}')
PY_RECONCILE

  if [[ -s "$merged/header-affected-objects.txt" || -s "$merged/owner-restored.txt" ]]; then
    echo 'RV32I_UNION_LINK=RECONCILE original_GNU_Kbuild_second_link=true'
    if ! make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j4 \
        --eval="vmlinux: KBUILD_VMLINUX_LIBS += $runtime_archive" \
        Image >"$merged/minic-relink-retry.log" 2>&1; then
      echo 'RV32I_UNION_RECONCILE=FAIL original_GNU_Kbuild_second_link' >&2
      tail -n 100 "$merged/minic-relink-retry.log" >&2
      exit 7
    fi
    python3 "$regen_guard" --mode stable --out "$out" \
      --evidence "$merged/generated-after-first-link.json"
  fi

  # Check ALL original owners after the final link. A post-header MiniC
  # replay legitimately has a NEW certified SHA; every other owner must
  # remain byte-identical to its immutable seven-shard candidate.
  python3 - "$out" "$merged" <<'PY_UNION'
from pathlib import Path
import hashlib
import sys
out, merged = map(Path, sys.argv[1:])
replay = {}
for line in (merged / 'header-replay-hashes.sha256').read_text().splitlines() if (merged / 'header-replay-hashes.sha256').exists() else []:
    digest, name = line.split('  ', 1)
    if name in replay:
        raise SystemExit(f'RV32I_UNION_OBJECTS=FAIL duplicate_replay={name}')
    replay[name] = digest
affected = set((merged / 'header-affected-objects.txt').read_text().splitlines())
if set(replay) != affected:
    raise SystemExit(f'RV32I_UNION_OBJECTS=FAIL replay_coverage={len(replay)} affected={len(affected)}')
drift = []
for record in (merged / 'minic-manifest.sha256').read_text().splitlines():
    expected, rel = record.split('  ', 1)
    assert rel.startswith('minic/'), rel
    owner = rel[len('minic/'):]
    expected = replay.get(owner, expected)
    actual = hashlib.sha256((out / owner).read_bytes()).hexdigest()
    if actual != expected:
        drift.append((owner, expected, actual))
if drift:
    with (merged / 'minic-owner-drift.tsv').open('w') as report:
        report.write('owner\texpected\tactual\n')
        for owner, expected, actual in drift:
            report.write(f'{owner}\t{expected}\t{actual}\n')
    print(f'RV32I_UNION_OBJECTS=FAIL mutated={len(drift)} inspected={len((merged / "minic-manifest.sha256").read_text().splitlines())}', file=sys.stderr)
    for owner, expected, actual in drift[:32]:
        print(f'RV32I_UNION_OWNER_DRIFT owner={owner} expected={expected} actual={actual}', file=sys.stderr)
    raise SystemExit(7)
print(f'RV32I_UNION_OBJECTS=PASS owners={len((merged / "minic-manifest.sha256").read_text().splitlines())} header_replayed={len(replay)} no_GNU_C_fallback=true')
PY_UNION
  test -s "$out/arch/riscv/boot/Image"
  riscv64-linux-gnu-readelf -h "$out/vmlinux" >"$merged/minic-vmlinux-elf.txt"
  grep -Eq 'Class:[[:space:]]+ELF32' "$merged/minic-vmlinux-elf.txt"
  printf 'RV32I_UNION_LINK=PASS ELF32=1 full_MiniC_C=true original_GNU_Kbuild=true image_sha256=%s QEMU_P1=NOT_RUN\n' \
    "$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)" | tee "$merged/union-result.txt"
  exit 0
fi
actual_version=$(make "${gnu_args[@]}" -s kernelversion)
if [[ "$actual_version" != 6.6.143 ]]; then
  echo "RV32I_LINUX=FAIL unexpected_kernel=$actual_version expected=6.6.143" >&2
  exit 2
fi
make "${gnu_args[@]}" defconfig >"$ev/defconfig.log" 2>&1
"$src/scripts/config" --file "$out/.config" \
  -e NONPORTABLE -e ARCH_RV32I -d ARCH_RV64I \
  -d EFI -d RISCV_ISA_C -d FPU -d SMP -d VGA_CONSOLE
make "${gnu_args[@]}" olddefconfig >"$ev/olddefconfig.log" 2>&1
# Linux 6.6 arch/riscv/Kconfig EFI explicitly "select RISCV_ISA_C". Disable EFI
# in the original Kconfig or olddefconfig silently re-enables compressed C.
# Linux's VGA_CONSOLE defaults to y on RISC-V despite no screen_info owner.
# It would make the otherwise valid original GNU 32-bit Image fail final LD.
# Select the headless serial-console target using original Kconfig options.
# This is a real original Kconfig audit, not a custom defconfig or kernel patch.
# Preserve the GENERATED config even when the next fail-closed check fires.
cp "$out/.config" "$ev/generated-kconfig.txt"
grep -E '^(CONFIG_(NONPORTABLE|ARCH_RV32I|ARCH_RV64I|32BIT|RISCV_ISA_C|FPU|SMP)=|# CONFIG_(ARCH_RV64I|RISCV_ISA_C|FPU|SMP) is not set)' "$out/.config" |
  sed 's/^/RV32I_LINUX_ACTUAL_KCONFIG /'
for setting in CONFIG_NONPORTABLE CONFIG_ARCH_RV32I CONFIG_32BIT; do
  grep -Fxq "$setting=y" "$out/.config" || {
    echo "RV32I_LINUX=FAIL Kconfig_missing=$setting" >&2; exit 3;
  }
done
for setting in CONFIG_ARCH_RV64I CONFIG_EFI CONFIG_RISCV_ISA_C CONFIG_FPU CONFIG_SMP CONFIG_VGA_CONSOLE; do
  grep -Fxq "# $setting is not set" "$out/.config" || {
    echo "RV32I_LINUX=FAIL Kconfig_wrong=$setting" >&2; exit 3;
  }
done
config_sha=$(sha256sum "$out/.config" | cut -d' ' -f1)
printf 'RV32I_LINUX_CONFIG=PASS original=6.6.143 arch=ARCH_RV32I no_C=1 no_FPU=1 no_SMP=1 sha256=%s\n' "$config_sha"
if [[ "$mode" == config ]]; then exit 0; fi
# The UNMODIFIED upstream kernel intentionally selects rv32ima for atomic /
# supervisor instructions. This is not the ISA used by MiniC's C emitter.
echo 'RV32I_LINUX_ISA_CONTRACT=PASS MiniC_C=rv32i original_Kbuild_asm=rv32ima separate_owners=true'
if [[ "$mode" == entry ]]; then
# Quick REAL owner front-run: the same original GNU Kbuild C source and
# existing MiniC Kbuild proxy, not a dummy fixture or a substitute for the
# full source-owner/Image/QEMU gate. Catch actual C/ABI blockers before the
# slow full GNU Image, then restore a byte-exact original GNU object.
preflight_owner=init/main.o
if ! make "${gnu_args[@]}" -j4 "$preflight_owner" >"$ev/gnu-owner-preflight.log" 2>&1; then
  echo "RV32I_LINUX_GNU_PRECHECK=BLOCKED original_owner=$preflight_owner" >&2
  tail -n 60 "$ev/gnu-owner-preflight.log" >&2
  exit 4
fi
gnu_owner_sha=$(sha256sum "$out/$preflight_owner" | cut -d' ' -f1)
rm -f "$out/$preflight_owner"
if ! MINIC="$MINIC_CC" REAL_CC=riscv64-linux-gnu-gcc \
      MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 MINIC_PRESERVE_FAILURE_INPUTS=1 \
      MINIC_KBUILD_TRACE="$ev/fast-kbuild-route.log" \
      make "${gnu_args[@]}" \
        CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" \
        "$preflight_owner" >"$ev/minic-owner-preflight.log" 2>&1; then
  echo "RV32I_LINUX_MINIC_PRECHECK=BLOCKED authentic_Kbuild_owner=$preflight_owner" >&2
  tail -n 80 "$ev/minic-owner-preflight.log" >&2
  exit 5
fi
test -s "$out/$preflight_owner"
rm -f "$out/$preflight_owner"
make "${gnu_args[@]}" "$preflight_owner" >"$ev/gnu-owner-restore.log" 2>&1
test "$(sha256sum "$out/$preflight_owner" | cut -d' ' -f1)" = "$gnu_owner_sha"
echo "RV32I_LINUX_MINIC_PRECHECK=PASS original_owner=$preflight_owner restored_GNU_object_sha=$gnu_owner_sha"
fi
# First establish the original GNU Image. A failure is a
# GNU/Kconfig/toolchain baseline blocker, not a MiniC defect.
# Stream a bounded, timestamped progress receipt without flooding the log.
# The complete original make output stays in gnu-image.log, not in GitHub logs.
start_seconds=$SECONDS
make "${gnu_args[@]}" -j4 Image >"$ev/gnu-image.log" 2>&1 &
gnu_pid=$!
while kill -0 "$gnu_pid" 2>/dev/null; do
  sleep 20
  kill -0 "$gnu_pid" 2>/dev/null || break
  completed=$(grep -cE '^[[:space:]]+(CC|AS|AR|LD|HOSTCC|HOSTLD)[[:space:]]+' "$ev/gnu-image.log" || true)
  current=$(tail -n 1 "$ev/gnu-image.log" | cut -c1-150)
  printf 'RV32I_GNU_KBUILD_PROGRESS elapsed_seconds=%s compile_and_link_events=%s last=%q\n' \
    "$((SECONDS-start_seconds))" "$completed" "$current"
done
if ! wait "$gnu_pid"; then
  echo 'RV32I_LINUX_GNU_BASELINE=BLOCKED original_Kbuild_Image_failed' >&2
  tail -n 90 "$ev/gnu-image.log" >&2
  exit 4
fi
gnu_image="$out/arch/riscv/boot/Image"
test -s "$gnu_image"
test -s "$out/vmlinux"
riscv64-linux-gnu-readelf -h "$out/vmlinux" >"$ev/gnu-vmlinux-elf.txt"
grep -Eq 'Class:[[:space:]]+ELF32' "$ev/gnu-vmlinux-elf.txt"
grep -Eq 'Machine:[[:space:]]+RISC-V' "$ev/gnu-vmlinux-elf.txt"
gnu_sha=$(sha256sum "$gnu_image" | cut -d' ' -f1)
printf 'RV32I_LINUX_GNU_BASELINE=PASS ELF32=1 original_Kbuild=1 Image_SHA=%s\n' "$gnu_sha"
if [[ "$mode" == golden ]]; then
  # The legacy GNU golden producer's complete ET_REL manifest, applied to
  # this exact ELF32/Image rather than borrowing the incompatible RV64 pool.
  python3 - "$out" "$prov/gcc-objects.sha256" <<'PY_GNU_OBJECTS'
from pathlib import Path
import hashlib
import struct
import sys
out, dest = map(Path, sys.argv[1:])
count = 0
with dest.open("w", encoding="utf-8") as manifest:
    for obj in sorted(out.rglob("*.o")):
        with obj.open("rb") as f:
            header = f.read(20)
        if (len(header) < 20 or header[:6] != b"\x7fELF\x01\x01"
                or struct.unpack_from("<H", header, 16)[0] != 1
                or struct.unpack_from("<H", header, 18)[0] != 243):
            continue
        digest = hashlib.sha256(obj.read_bytes()).hexdigest()
        manifest.write(f"{digest}  {obj.relative_to(out)}\n")
        count += 1
print(f"RV32I_GNU_OBJECT_POOL=PASS ELF32=1 total={count}")
PY_GNU_OBJECTS
  python3 "$GITHUB_WORKSPACE/tools/ci/linux-runtime-gcc-full-c-objects-v1.py" \
    --out "$out" --manifest "$prov/gcc-objects.sha256" \
    --output "$prov/full-c-objects.txt" \
    --excluded "$prov/non-c-objects.tsv" --min-c-objects 1200
  owner_count=$(sed '/^#/d;/^$/d' "$prov/full-c-objects.txt" | wc -l)
  {
    echo 'schema=linux-gnu-exact-config-v1'
    echo 'linux_release=6.6.143'
    echo 'linux_archive_sha256=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932'
    echo 'elf_class=ELF32'
    echo 'isa=rv32i'
    echo 'abi=ilp32'
    echo "config_sha256=$config_sha"
    echo "gcc_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-gcc)" | cut -d' ' -f1)"
    echo "gnu_ld_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-ld)" | cut -d' ' -f1)"
    echo "gnu_as_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-as)" | cut -d' ' -f1)"
    echo "image_sha256=$gnu_sha"
    echo "c_owner_count=$owner_count"
    echo 'qemu_contract=not_run'
  } >"$prov/gcc-baseline.txt"
  echo "RV32I_GNU_GOLDEN=PASS full_c_owners=$owner_count config=$config_sha Image_SHA=$gnu_sha"
  exit 0
fi
# Same real-Kbuild subset/receipt mechanism as the frozen RV64 2064.
# Select independent kernel language/ABI owner classes; no custom dummy C.
owners=(init/main.o kernel/fork.o mm/page_alloc.o lib/string.o arch/riscv/kernel/traps.o)
printf '%s\n' "${owners[@]}" >"$ev/owners.txt"
: >"$ev/minic-success.txt"
: >"$ev/minic-objects.sha256"
: >"$ev/kbuild-route.log"
for owner in "${owners[@]}"; do
  test -s "$out/$owner"
  rm -f "$out/$owner"
  MINIC="$MINIC_CC" REAL_CC=riscv64-linux-gnu-gcc \
    MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 \
    MINIC_KBUILD_SUCCESS_TRACE="$ev/minic-success.txt" \
    MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/minic-objects.sha256" \
    MINIC_KBUILD_TRACE="$ev/kbuild-route.log" \
    MINIC_PRESERVE_FAILURE_INPUTS=1 \
    make "${gnu_args[@]}" \
      CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" \
      "$owner" >"$ev/$(echo "$owner" | tr / _).log" 2>&1 || {
        echo "RV32I_LINUX_MINIC=BLOCKED owner=$owner (original GNU Image remains proven)" >&2
        tail -n 100 "$ev/$(echo "$owner" | tr / _).log" >&2
        exit 5
      }
  test -s "$out/$owner"
  riscv64-linux-gnu-readelf -h "$out/$owner" |
    grep -Eq 'Class:[[:space:]]+ELF32'
  grep -Fxq "$owner" "$ev/minic-success.txt"
  grep -Fq "output=$owner" "$ev/kbuild-route.log"
done
python3 - "$ev" "$out" <<'PY'
from pathlib import Path
import hashlib
import sys
ev, out = map(Path, sys.argv[1:])
owners = (ev / "owners.txt").read_text().splitlines()
success = (ev / "minic-success.txt").read_text().splitlines()
assert len(owners) == 5, owners
# Authentic Kbuild may rebuild C prerequisites (modpost's empty.o, VDSO
# hwprobe.o) when CC changes for the first selected owner. They were also
# compiled through MiniC, not GCC fallbacks. Audit their real receipts while
# requiring the five requested owners exactly once and in selection order.
selected = [owner for owner in success if owner in owners]
assert selected == owners and len(set(success)) == len(success), (owners, success)
hashes = (ev / "minic-objects.sha256").read_text().splitlines()
assert len(hashes) == len(success), (success, hashes)
for owner, entry in zip(success, hashes):
    expected, path = entry.split("  ", 1)
    assert path == owner, (owner, entry)
    assert hashlib.sha256((out / owner).read_bytes()).hexdigest() == expected
route = (ev / "kbuild-route.log").read_text()
assert route.count("minic rv32i_origin_audit=1 isa=rv32i abi=ilp32 ") == len(success)
assert route.count("assemble input=") == len(success)
print(f"RV32I_LINUX_MINIC=PASS original_Kbuild_owners=5/5 additional_Kbuild_C={len(success)-5} GNU_PP=1 MiniC_RV32I=1 GNU_AS=1 SHA=1 ELF32=1 no_C_fallback=1")
PY
# Reuse ORIGINAL GNU Kbuild link rather than creating an alternate linker.
# Linux 6.6's documented KBUILD_NOCMDDEP=1 suppresses only command-line
# difference rebuilds. Without it, switching CC back from the MiniC proxy to
# GNU silently recompiles the five MiniC objects with GCC before relinking.
# Source and dependency timestamps still govern normal Kbuild prerequisites;
# the object SHA audit below proves that the final Image kept MiniC objects.
if ! make "${gnu_args[@]}" KBUILD_NOCMDDEP=1 -j4 Image >"$ev/minic-relink.log" 2>&1; then
  echo 'RV32I_LINUX_RELINK=BLOCKED genuine_Kbuild_5_owner_Image' >&2
  tail -n 90 "$ev/minic-relink.log" >&2
  exit 6
fi
test -s "$gnu_image"
python3 - "$ev" "$out" <<'PY'
from pathlib import Path
import hashlib
import sys
ev, out = map(Path, sys.argv[1:])
receipts = (ev / "minic-objects.sha256").read_text().splitlines()
for entry in receipts:
    expected, owner = entry.split("  ", 1)
    actual = hashlib.sha256((out / owner).read_bytes()).hexdigest()
    assert actual == expected, (owner, expected, actual)
print(f"RV32I_LINUX_RELINK_OBJECT_SHA=PASS MiniC_objects_preserved={len(receipts)} selected=5")
PY
printf 'RV32I_LINUX_RELINK=PASS original_Kbuild=1 candidate_owners=5 MiniC_object_SHA_preserved=1 Image_SHA=%s QEMU_P1=NOT_RUN full_MiniC=NOT_RUN\n' "$(sha256sum "$gnu_image" | cut -d' ' -f1)"

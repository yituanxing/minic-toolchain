#!/usr/bin/env bash
# Experimental opt-in: exactly four MiniC TU recompiles with a conservative
# first-write cross-basic-block reuse proof. No mutation of the SHA-certified
# 2064-object pool; full GNU provenance and same-Image P1 boots are mandatory.
set -Eeuo pipefail
[[ -n "$minic" && -n "$gold_snapshot" && -n "$ev" && -n "$out" ]] || exit 8
[[ "$n" -eq 2064 ]] || { echo "P1_DEFINED_CROSSBLOCK=ERROR object_count"; exit 8; }
single_owner="${COHORT_P1_SINGLE_OWNER:-0}"
# Exact opt-in only: replay the *existing*, semantically guarded reduction of
# discarded plain-local address comparisons for one TU on the fully-MiniC
# 16KiB runtime profile. GDB observes the same pinned candidate Image after
# its three untouched normal QEMU runs; diagnostic events are not P1 success.
same_image_irq=0
if [[ "$single_owner" == 1 && "${COHORT_P1_DISCARD_EQ_BOTH:-0}" == 1 &&
      "${COHORT_P1_GDB_STACK:-0}" == 1 ]] &&
    git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-allpure-gdb]'; then
  same_image_irq=1
fi
if [[ "$single_owner" == 1 ]]; then
  # Preserve old codegen in every function except one suspect frame;
  # only ip6_finish_output2 is permitted to use the experimental allocator.
  if [[ "${COHORT_P1_DISCARD_EQ:-0}" == 1 ]]; then
    # Test semantic removal of genuinely discarded (&a == &b) with two
    # local variables; do not touch stack slot reuse in this experiment.
    if [[ "${COHORT_P1_DISCARD_EQ_BOTH:-0}" == 1 ]]; then
      if [[ "$same_image_irq" == 1 ]]; then
        export MINIC_P1_DISCARD_EQ_FUNCTION=""
        echo "P1_DISCARD_EQ=ONE_TU_ALL_PURE_DIAGNOSTIC owner=net/ipv6/ip6_output.o"
      else
        export MINIC_P1_DISCARD_EQ_FUNCTION=ip6_pair
        echo "P1_DISCARD_EQ=FUNCTION_RESTRICTED names=ip6_finish_output2,ip6_xmit"
      fi
    else
      export MINIC_P1_DISCARD_EQ_FUNCTION=ip6_finish_output2
      echo "P1_DISCARD_EQ=FUNCTION_RESTRICTED name=ip6_finish_output2"
    fi
  else
    export MINIC_P1_CROSSBLOCK_FUNCTION=ip6_finish_output2
  fi
  dest="$ev/p1-defined-ip6-only"
else
  dest="$ev/p1-defined-crossblock"
fi
mkdir -p "$dest"
cat >"$dest/selected.txt" <<'OWNERS'
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_output.o
net/core/dev.o
OWNERS
if [[ "$single_owner" == 1 ]]; then
  # Retain every other certified MiniC object and use GCC only for
  # tcp_ipv6.o. Recompile ip6_output.o with the opt-in compiler.
  printf '%s\n' net/ipv6/ip6_output.o >"$dest/selected.txt"
fi
python3 - "$dest/selected.txt" "$objects_file" <<'PY_SET'
from pathlib import Path
import sys
a,b=map(Path,sys.argv[1:])
names=a.read_text().splitlines()
assert len(names) in (1,4) and len(set(names))==len(names)
assert set(names)<=set(b.read_text().splitlines())
print(f"P1_DEFINED_CROSSBLOCK=SELECTED owners={len(names)}")
PY_SET
(cd "$ev" && sha256sum --quiet -c minic-manifest.sha256)
printf 'compiler_sha256=%s\n' "$(sha256sum "$minic"|cut -d' ' -f1)" >"$dest/provenance.txt"
rm -rf -- "$out"
cp -a --reflink=auto "$gold_snapshot" "$out"
COHORT_FRAME_CAPTURE_ONLY="$single_owner" MINIC_CORE_FRAME_TRACE=1 \
MINIC_CORE_FRAME_TRACE_FILTER="ip6_finish_output2,ip6_xmit" \
python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
  --mode replay --out "$out" --golden-out "$gold_snapshot" \
  --evidence "$dest/replay-evidence.json" --selected "$dest/selected.txt" \
  --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
  --minic "$minic" --success-trace "$dest/success.txt" \
  --digest-trace "$dest/candidates.sha256" >"$dest/replay.log" 2>&1 || {
    tail -n 65 "$dest/replay.log"; exit 8;
  }
python3 "$repo/tools/ci/linux-runtime-minic-route-audit-v1.py" \
  --objects-file "$dest/selected.txt" \
  --success-trace "$dest/success.txt" \
  --object-digest-trace "$dest/candidates.sha256" \
  --candidates "$out" >"$dest/route-audit.log"
if [[ "$single_owner" == 1 ]]; then
  # The instrumentation prints to a per-TU stderr file, not replay.log.
  stderr="$out/net/ipv6/ip6_output.minic-stage2.minic.stderr"
  [[ -s "$stderr" ]] || { echo "P1_DEFINED_CROSSBLOCK=ERROR no_minic_stderr"; exit 8; }
  grep -E '^CORE_FRAME' "$stderr" >"$dest/frame-audit.log" || {
    echo "P1_DEFINED_CROSSBLOCK=ERROR missing_frame_trace"; exit 8;
  }
  python3 "$repo/tools/ci/linux-runtime-p1-frame-component-parse-v1.py" \
    "$dest/frame-audit.log" "$dest/frame-components.tsv" "$dest/frame-objects.tsv"
  awk -F '\t' '$1=="ip6_finish_output2" || $1=="ip6_xmit" {
    print "P1_DEFINED_CROSSBLOCK=FRAME function="$1" bytes="$2" objects="$10
    seen++
  } END {exit seen!=2}' "$dest/frame-components.tsv" || {
    echo "P1_DEFINED_CROSSBLOCK=ERROR missing_both_ipv6_frames"; exit 8;
  }
  python3 - "$dest/frame-components.tsv" <<'P1_FOCUS_BOUNDS'
import csv,sys
with open(sys.argv[1]) as f:
    rows={r["function"]:r for r in csv.DictReader(f,delimiter="\t")}
finish=int(rows["ip6_finish_output2"]["frame_bytes"])
xmit=int(rows["ip6_xmit"]["frame_bytes"])
assert 0<finish<4064, f"selected frame did not shrink: {finish}"
if __import__("os").environ.get("COHORT_P1_DISCARD_EQ_BOTH") == "1":
    assert 0<xmit<2288, f"selected ip6_xmit did not shrink: {xmit}"
else:
    assert xmit==2288, f"unselected ip6_xmit unexpectedly changed: {xmit}"
print(f"P1_DEFINED_CROSSBLOCK=FRAME_ISOLATED selected=ip6_finish_output2 new={finish} old=4064 unaffected=ip6_xmit frame={xmit}")
P1_FOCUS_BOUNDS
fi
(cd "$ev" && sha256sum --quiet -c minic-manifest.sha256) || {
  echo "P1_DEFINED_CROSSBLOCK=ERROR original_pool_mutated"; exit 8;
}
mkdir -p "$dest/pool"
cp -a --reflink=auto "$ev/minic" "$dest/pool/minic"
printf 'owner\toriginal_sha256\tcandidate_sha256\tdifferent\n' >"$dest/object-diff.tsv"
changed=0
while IFS= read -r target; do
  old_sha=$(sha256sum "$ev/minic/$target"|cut -d' ' -f1)
  new_sha=$(sha256sum "$out/$target"|cut -d' ' -f1)
  python3 - "$ev/minic/$target" "$out/$target" <<'PY_ABI'
from pathlib import Path
import struct,sys
a,b=[Path(x).read_bytes()[:52] for x in sys.argv[1:]]
for h in (a,b):
    assert len(h)==52 and h[:6]==b'\x7fELF\x02\x01'
    assert struct.unpack_from('<H',h,16)[0]==1
    assert struct.unpack_from('<H',h,18)[0]==243
assert a[48:52]==b[48:52], "RISC-V ELF flags drift"
PY_ABI
  cp -a "$out/$target" "$dest/pool/minic/$target"
  changed_this=0
  if [[ "$new_sha" != "$old_sha" ]]; then
    changed_this=1
    changed=$((changed+1))
  fi
  printf '%s\t%s\t%s\t%d\n' "$target" "$old_sha" "$new_sha" "$changed_this" \
    >>"$dest/object-diff.tsv"
  echo "P1_DEFINED_CROSSBLOCK=OBJECT owner=$target changed=$changed_this"
done <"$dest/selected.txt"
[[ "$changed" -gt 0 ]] || {
  echo "P1_DEFINED_CROSSBLOCK=INCONCLUSIVE no_candidate_object_changed"; exit 8;
}
python3 - "$ev" "$dest/pool/minic" "$dest/selected.txt" <<'PY_VERIFY'
import hashlib,sys
from pathlib import Path
ev,overlay,selected=map(Path,sys.argv[1:])
changed=set(selected.read_text().splitlines())
untouched=matched=0
for line in (ev/"minic-manifest.sha256").read_text().splitlines():
    digest,path=line.split(None,1)
    assert path.startswith("minic/")
    target=path[6:]
    actual=hashlib.sha256((overlay/target).read_bytes()).hexdigest()
    if target in changed:
        matched += 1
    else:
        assert actual==digest, "unrelated MiniC owner changed: "+target
        untouched += 1
assert matched in (1,4) and untouched==2064-matched
print(f"P1_DEFINED_CROSSBLOCK=POOL_ISOLATED changed_targets={matched} original_neighbors={untouched}")
PY_VERIFY
export COHORT_P1_EXPERIMENTAL_POOL="$dest/pool/minic"
last_selection="none"
last_link_source_dir=""
last_candidate_delta=""
printf 'trial\timage_sha256\tverdict\n' >"$dest/p1-repeats.tsv"
pinned=""
passes=0
mode=prefix
if [[ "$single_owner" == 1 ]]; then
  if [[ "$same_image_irq" == 1 ]]; then
    mode=prefix
    echo "P1_DEFINED_CROSSBLOCK=ALL_MINIC_16K original=2063 modified=1 gcc=0"
  else
    mode=exclude_list
    printf '%s\n' net/ipv6/tcp_ipv6.o >"$ev/p1_gcc_rescue_set.txt"
    echo "P1_DEFINED_CROSSBLOCK=SINGLE_OWNER_ONLY minic_modified=net/ipv6/ip6_output.o gcc_owner=net/ipv6/tcp_ipv6.o"
  fi
fi
for attempt in 1 2 3; do
  trial "p1_defined_crossblock_$attempt" "$n" p1 "$mode"
  [[ -n "$pinned" ]] || pinned="$image_sha"
  [[ "$image_sha" == "$pinned" ]] || {
    echo "P1_DEFINED_CROSSBLOCK=ERROR image_sha_drift"; exit 8;
  }
  printf '%s\t%s\t%s\n' "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$dest/p1-repeats.tsv"
  if [[ "$TRIAL_VERDICT" == PASS ]]; then passes=$((passes+1)); fi
done
# Diagnose the actual experimental Image, not the all-MiniC control.
# The three boots above used the exact same pinned Image SHA. The last
# linked vmlinux is from that same object selection.
if [[ "${COHORT_P1_GDB_STACK:-0}" == 1 ]]; then
  experimental_image="$ev/trials/p1_defined_crossblock_1/Image"
  [[ -s "$experimental_image" && -s "$out/vmlinux" ]] || {
    echo "P1_DEFINED_CROSSBLOCK_GDB=INCONCLUSIVE missing_same_selection_image_or_vmlinux"
    exit 8
  }
  [[ "$(sha256sum "$experimental_image" | cut -d' ' -f1)" == "$pinned" ]] || {
    echo "P1_DEFINED_CROSSBLOCK_GDB=ERROR experimental_image_sha_mismatch"
    exit 8
  }
  echo "P1_DEFINED_CROSSBLOCK_GDB=START image_sha=$pinned experimental_only=true"
  if bash "$repo/tools/ci/linux-runtime-p1-gdb-stack-v1.sh" \
       "$experimental_image" "$out/vmlinux" \
       "$ev/runtime-initramfs.cpio.gz" "$LINUX_RUNTIME_CUSTOM_DTB" \
       "$ev/p1-gdb-stack-experiment" 65; then
    echo "P1_DEFINED_CROSSBLOCK_GDB=CAPTURED no_P1_pass_inference=true"
    # Correlate the OBSERVED per-CPU IRQ base with the saved trapped SP
    # from the three normal (non-GDB) executions of the EXACT same Image.
    python3 - "$ev" "$dest/p1-irq-attribution.tsv" "$pinned" <<'PY_IRQ_ATTRIBUTION'
from pathlib import Path
import re, sys
ev, report, expected_sha = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
gdb = (ev / "p1-gdb-stack-experiment/gdb.log").read_text(errors="replace")
m = re.search(r"P1_GDB_IRQ_LAYOUT irq_base=0x([0-9a-fA-F]+) irq_top=0x([0-9a-fA-F]+)", gdb)
irq = re.search(r"P1_GDB_FRAME name=handle_riscv_irq [^\n]*sp=0x([0-9a-fA-F]+)", gdb)
if not m or not irq:
    print("P1_DEFINED_CROSSBLOCK_IRQ=INCONCLUSIVE missing_layout_or_irq_entry")
    raise SystemExit(0)
base, top = (int(m.group(i), 16) for i in (1, 2))
entry = int(irq.group(1), 16)
if top - base != 16384 or entry != top:
    print("P1_DEFINED_CROSSBLOCK_IRQ=INCONCLUSIVE invalid_irq_stack_boundary_or_entry")
    raise SystemExit(0)
rows = []
for index in (1, 2, 3):
    log = (ev / f"trials/p1_defined_crossblock_{index}/qemu/rdinit-init.log").read_text(errors="replace")
    crash = log.split("Insufficient stack space to handle exception!", 1)
    mm = re.search(r"\bsp\s*:\s*(?:0x)?([0-9a-fA-F]{16})", crash[-1])
    if len(crash) != 2 or not mm:
        print(f"P1_DEFINED_CROSSBLOCK_IRQ=INCONCLUSIVE trial={index} no_captured_fault_sp")
        raise SystemExit(0)
    fault_sp = int(mm.group(1), 16)
    delta = base - fault_sp
    rows.append((index, f"0x{fault_sp:x}", delta))
report.write_text(
    "trial\ttrapped_sp\tbytes_below_irq_stack_base\n" +
    "".join(f"{i}\t{sp}\t{delta}\n" for i, sp, delta in rows)
)
if all(0 < row[2] <= 4096 for row in rows):
    print("P1_DEFINED_CROSSBLOCK_IRQ=VERIFIED irq_base=0x%x irq_top=0x%x "
          "trapped_sp_below_base_bytes=%s trials=3 same_image_sha=%s "
          "debugger_not_a_P1_oracle=true" %
          (base, top, ",".join(str(row[2]) for row in rows), expected_sha))
else:
    print("P1_DEFINED_CROSSBLOCK_IRQ=INCONCLUSIVE irq_base=0x%x deltas=%s" %
          (base, ",".join(str(row[2]) for row in rows)))
PY_IRQ_ATTRIBUTION
  else
    echo "P1_DEFINED_CROSSBLOCK_GDB=INCONCLUSIVE no_P1_pass_inference=true"
  fi
fi
if [[ "$single_owner" == 1 ]]; then
  if [[ "$same_image_irq" == 1 ]]; then
    echo "P1_DEFINED_CROSSBLOCK=COMPLETE modified_minic=1 gcc_owners=0 minic_owners=2064 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
  else
    echo "P1_DEFINED_CROSSBLOCK=COMPLETE modified_minic=1 gcc_owners=1 minic_owners=2063 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
  fi
else
  echo "P1_DEFINED_CROSSBLOCK=COMPLETE modified_minic=4 gcc_owners=0 minic_owners=2064 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
fi
exit 0

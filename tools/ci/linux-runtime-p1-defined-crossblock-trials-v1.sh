#!/usr/bin/env bash
# Experimental opt-in: exactly four MiniC TU recompiles with a conservative
# first-write cross-basic-block reuse proof. No mutation of the SHA-certified
# 2064-object pool; full GNU provenance and same-Image P1 boots are mandatory.
set -Eeuo pipefail
[[ -n "$minic" && -n "$gold_snapshot" && -n "$ev" && -n "$out" ]] || exit 8
[[ "$n" -eq 2064 ]] || { echo "P1_DEFINED_CROSSBLOCK=ERROR object_count"; exit 8; }
single_owner="${COHORT_P1_SINGLE_OWNER:-0}"
# One 4-owner read-only ROI screen, no Image relink. Existing receiver and
# experimental compiler only; default runtime experiment unchanged.
rx_roi=0
rx_bundle=0
rx_bundle_page=0
rx_bundle_dev=0
rx_bundle_timer=0
rx_bundle_neigh=0
if [[ "$single_owner" != 1 && "${COHORT_P1_DISCARD_EQ:-0}" != 1 ]] &&
  git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-rx-bundle-timer]'; then
  rx_bundle=1
  rx_bundle_page=1
  rx_bundle_dev=1
  rx_bundle_timer=1
  unset MINIC_P1_CROSSBLOCK_FUNCTION
  echo "P1_RX_BUNDLE_TIMER=START owners=7 sixth_net_dev_preserved=true timer_owner=kernel/time/hrtimer.o repeats=6"
  # The 2026-10-13 IRQ/IPv6 preemption trace proves that improving only
  # the timer/PELT leaf is insufficient. Experiment with the already-known
  # 3024-B __neigh_create() frame as ONE additional RV64 C owner. Keep the
  # seven prior experimental owners, original 2056 neighbour .o hashes,
  # Kbuild flags and full-MiniC object provenance unchanged.
  if git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-neighbour-owner]'; then
    rx_bundle_neigh=1
    echo "P1_RX_NEIGH=START owners=8 newly_selected=net/core/neighbour.o 2064_frozen_pool=true 12_same_image_boots=true"
  fi
fi
if [[ "$single_owner" != 1 && "${COHORT_P1_DISCARD_EQ:-0}" != 1 ]] &&
  git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-rx-bundle-dev]'; then
  rx_bundle=1
  rx_bundle_page=1
  rx_bundle_dev=1
  unset MINIC_P1_CROSSBLOCK_FUNCTION
  echo "P1_RX_BUNDLE_DEV=START owners=6 extra=net/core/dev.o original_16K=true six_same_Image_QEMU=true"
fi
if [[ "$single_owner" != 1 && "${COHORT_P1_DISCARD_EQ:-0}" != 1 ]] &&
  git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-rx-bundle-page]'; then
  rx_bundle=1
  rx_bundle_page=1
  unset MINIC_P1_CROSSBLOCK_FUNCTION
  echo "P1_RX_BUNDLE_PAGE=START owners=5 additional_owner=mm/page_alloc.o normal_qemu_repeats=3"
fi
if [[ "$single_owner" != 1 && "${COHORT_P1_DISCARD_EQ:-0}" != 1 ]] &&
  git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-rx-bundle]'; then
  rx_bundle=1
  unset MINIC_P1_CROSSBLOCK_FUNCTION
  echo "P1_RX_BUNDLE=START owners=4 frozen_minic_neighbors=2060 unchanged_16K=true three_boots=true experimental_not_certified=true"
fi
if [[ "$single_owner" != 1 && "${COHORT_P1_DISCARD_EQ:-0}" != 1 ]] &&
  git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-rx-roi]'; then
  rx_roi=1
  # No function restriction: measure global high-ROI CFG-local scratch
  # allocation across these four selected TUs, never enable in production.
  unset MINIC_P1_CROSSBLOCK_FUNCTION
  echo "P1_RX_ROI=START method=first_write_single_block_no_address_escape owners=4 no_kernel_link=true"
fi
if [[ "$single_owner" == 1 ]]; then
  # Preserve old codegen in every function except one suspect frame;
  # only ip6_finish_output2 is permitted to use the experimental allocator.
  if [[ "${COHORT_P1_DISCARD_EQ:-0}" == 1 ]]; then
    # Test semantic removal of genuinely discarded (&a == &b) with two
    # local variables; do not touch stack slot reuse in this experiment.
    if [[ "${COHORT_P1_DISCARD_EQ_BOTH:-0}" == 1 ]]; then
      export MINIC_P1_DISCARD_EQ_FUNCTION=ip6_pair
      echo "P1_DISCARD_EQ=FUNCTION_RESTRICTED names=ip6_finish_output2,ip6_xmit"
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
if [[ "$rx_bundle" == 1 ]]; then
  cat >"$dest/selected.txt" <<'OWNERS_BUNDLE'
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_input.o
mm/slub.o
mm/page_alloc.o
net/core/dev.o
kernel/time/hrtimer.o
OWNERS_BUNDLE
if [[ "$rx_bundle_neigh" == 1 ]]; then
  printf '%s\n' net/core/neighbour.o >>"$dest/selected.txt"
fi
elif [[ "$rx_roi" == 1 ]]; then
  cat >"$dest/selected.txt" <<'OWNERS_RX'
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_input.o
mm/slub.o
mm/page_alloc.o
OWNERS_RX
else
  cat >"$dest/selected.txt" <<'OWNERS'
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_output.o
net/core/dev.o
OWNERS
fi
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
assert len(names) in (1,4,5,6,7,8) and len(set(names))==len(names)
assert set(names)<=set(b.read_text().splitlines())
print(f"P1_DEFINED_CROSSBLOCK=SELECTED owners={len(names)}")
PY_SET
(cd "$ev" && sha256sum --quiet -c minic-manifest.sha256)
printf 'compiler_sha256=%s\n' "$(sha256sum "$minic"|cut -d' ' -f1)" >"$dest/provenance.txt"
rm -rf -- "$out"
cp -a --reflink=auto "$gold_snapshot" "$out"
replay_selected="$dest/selected.txt"
if [[ "$rx_bundle_neigh" == 1 ]]; then
  # Avoid recomputing the prior seven owners merely to discover that a
  # proposed eighth owner has NO frame ROI. This first pass uses the
  # exact same immutable Kbuild .cmd flags and MiniC binary, and appends
  # the authenticated object hash to the subsequent seven-owner evidence.
  printf '%s\n' net/core/neighbour.o >"$dest/neighbour-only-selected.txt"
  grep -Fxv 'net/core/neighbour.o' "$dest/selected.txt" >"$dest/prior-seven-selected.txt"
  [[ $(wc -l <"$dest/prior-seven-selected.txt") -eq 7 ]] || {
    echo "P1_RX_NEIGH=FAIL malformed_seven_owner_selection"; exit 8;
  }
  COHORT_FRAME_CAPTURE_ONLY=1 MINIC_CORE_FRAME_TRACE=1 \
  MINIC_CORE_FRAME_TRACE_FILTER="__neigh_create" \
  python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
    --mode replay --out "$out" --golden-out "$gold_snapshot" \
    --evidence "$dest/neighbour-first-evidence.json" \
    --selected "$dest/neighbour-only-selected.txt" \
    --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
    --minic "$minic" --success-trace "$dest/neighbour-success.txt" \
    --digest-trace "$dest/neighbour-candidates.sha256" >"$dest/neighbour-first.log" 2>&1 || {
      tail -n 60 "$dest/neighbour-first.log"; exit 8;
    }
  python3 - "$out/net/core/neighbour.minic-stage2.minic.stderr" "$dest" <<'PY_NEIGH_FIRST'
from pathlib import Path
import re
import sys
log, dest = map(Path, sys.argv[1:])
if not log.is_file():
    raise SystemExit("P1_RX_NEIGH=FAIL missing_neighbour_compiler_trace")
data = log.read_text(errors="replace")
frames = {m.group(1): int(m.group(2)) for m in
          re.finditer(r"^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b", data, re.M)}
frame = frames.get("__neigh_create")
baseline = 3024
if frame is None or not 0 < frame <= baseline - 512:
    raise SystemExit(f"P1_RX_NEIGH=NO_ROI baseline={baseline} candidate={frame} min_saving=512 no_link=1 no_seven_recompile=1")
(dest/"neighbour-frame.tsv").write_text(
    f"owner\tfunction\tbaseline_frame\tcandidate_frame\tsaving\n"
    f"net/core/neighbour.o\t__neigh_create\t{baseline}\t{frame}\t{baseline-frame}\n")
print(f"P1_RX_NEIGH=EARLY_ROI_PASS baseline={baseline} frame={frame} saved={baseline-frame} only_one_C_owner_compiled=1")
PY_NEIGH_FIRST
  replay_selected="$dest/prior-seven-selected.txt"
fi
# Deliberately combine TWO already-tested mechanisms to repair the remaining
# 16KiB IRQ budget, without changing the 2064-object frozen source pool.
# (1) conservative across-CFG stack coloring, as in prior eight-owner runs;
# (2) independently GCC/QEMU-tested elimination of discarded local-address
#     equality before CoreObject materialization.
# This is an exact eight-owner opt-in, NOT global production promotion.
if git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-combine-discard]'; then
  [[ "$rx_bundle_timer" == 1 && "$rx_bundle_neigh" == 1 &&
     "$single_owner" != 1 ]] || {
    echo "P1_COMBINED_DISCARD=FAIL require_exact_eight_owner_bundle" >&2
    exit 8
  }
  export MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1
  echo "P1_COMBINED_DISCARD=START exact_owners=8 gcc_c=0 2064_frozen=true pure_expression_scope=all_selected_functions"
fi
COHORT_FRAME_CAPTURE_ONLY="$(if [[ "$rx_roi" == 1 || "$rx_bundle" == 1 ]]; then echo 1; else echo "$single_owner"; fi)" MINIC_CORE_FRAME_TRACE=1 \
MINIC_CORE_FRAME_TRACE_FILTER="$(if [[ "$rx_roi" == 1 || "$rx_bundle" == 1 ]]; then echo 'ip6_finish_output2,ip6_xmit,tcp_v6_rcv,tcp_v6_do_rcv,tcp_conn_request,tcp_rcv_state_process,___slab_alloc,net_rx_action,process_backlog,__dev_queue_xmit,hrtimer_interrupt,__hrtimer_run_queues,__run_hrtimer,hrtimer_run_softirq,__neigh_create'; else echo 'ip6_finish_output2,ip6_xmit'; fi)" \
python3 "$repo/tools/ci/linux-runtime-kbuild-regeneration-guard-v1.py" \
  --mode replay --out "$out" --golden-out "$gold_snapshot" \
  --evidence "$dest/replay-evidence.json" --selected "$replay_selected" \
  --wrapper "$repo/tests/external/linux/stage2_kbuild_cc.sh" \
  --minic "$minic" --success-trace "$dest/success.txt" \
  --digest-trace "$dest/candidates.sha256" >"$dest/replay.log" 2>&1 || {
    tail -n 65 "$dest/replay.log"; exit 8;
  }
if [[ "$rx_bundle_neigh" == 1 ]]; then
  # Both independent MiniC invocations are required; the final route
  # audit authenticates all eight produced object bytes after replay.
  cat "$dest/neighbour-success.txt" >>"$dest/success.txt"
  cat "$dest/neighbour-candidates.sha256" >>"$dest/candidates.sha256"
fi
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
assert matched in (1,4,5,6,7,8) and untouched==2064-matched
print(f"P1_DEFINED_CROSSBLOCK=POOL_ISOLATED changed_targets={matched} original_neighbors={untouched}")
PY_VERIFY
if [[ "$rx_roi" == 1 ]]; then
  # Parse ORIGINAL compiler records for each exact ELF owner separately.
  # Do not use the legacy multi-TU component parser (duplicate inlined
  # helper names), and do not claim pass without per-TU evidence.
  python3 - "$out" "$dest" <<'PY_RX_FRAME'
import pathlib,re,sys
out,dest=map(pathlib.Path,sys.argv[1:])
baseline={
 'net/ipv6/tcp_ipv6':{'tcp_v6_rcv':1760,'tcp_v6_do_rcv':1104},
 'net/ipv4/tcp_input':{'tcp_conn_request':1312,'tcp_rcv_state_process':1776},
 'mm/slub':{'___slab_alloc':976},
 'mm/page_alloc':{'__alloc_pages':352,'__alloc_pages_slowpath':1024},
}
total=0
rows=['owner\tfunction\toriginal_frame\tcandidate_frame\treduction']
for owner,items in baseline.items():
    log=out/(owner+'.minic-stage2.minic.stderr')
    if not log.is_file():
        raise SystemExit(f'P1_RX_ROI=ERROR missing_compiler_trace={log}')
    data=log.read_text(errors='replace')
    (dest/(owner.replace('/','_')+'.frame.log')).write_text(
       '\n'.join(x for x in data.splitlines() if x.startswith('CORE_FRAME'))+'\n')
    recorded={}
    for m in re.finditer(r'^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b',data,re.M):
        recorded[m.group(1)]=int(m.group(2))
    for name,original in items.items():
        frame=recorded.get(name)
        if frame is None or not(0<frame<=original):
            raise SystemExit(f'P1_RX_ROI=ERROR function={name} old={original} candidate={frame}')
        saved=original-frame
        total+=saved
        rows.append(f'{owner}.o\t{name}\t{original}\t{frame}\t{saved}')
        print(f'P1_RX_ROI=FRAME owner={owner}.o function={name} old={original} new={frame} saved={saved}')
(dest/'rx-roi.tsv').write_text('\n'.join(rows)+'\n')
print(f'P1_RX_ROI=COMPLETE owners=4 functions={sum(map(len,baseline.values()))} isolated_frame_savings={total} changed_original_pool=false linked_image=false runtime_certified=false')
PY_RX_FRAME
  exit 0
fi
if [[ "$rx_bundle" == 1 ]]; then
  python3 - "$out" "$dest" <<'PY_RX_BUNDLE'
import pathlib,re,sys
out,dest=map(pathlib.Path,sys.argv[1:])
baseline={
 'net/ipv6/ip6_output':{'ip6_finish_output2':4064,'ip6_xmit':2288},
 'net/ipv6/tcp_ipv6':{'tcp_v6_rcv':1760,'tcp_v6_do_rcv':1104},
 'net/ipv4/tcp_input':{'tcp_conn_request':1312,'tcp_rcv_state_process':1776},
 'mm/slub':{'___slab_alloc':976},
 'mm/page_alloc':{'__alloc_pages':352,'__alloc_pages_slowpath':1024},
}
total=0
for owner,items in baseline.items():
    log=out/(owner+'.minic-stage2.minic.stderr')
    if not log.is_file(): raise SystemExit(f'P1_RX_BUNDLE=ERROR missing_trace={log}')
    records={}
    for m in re.finditer(r'^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b',log.read_text(errors='replace'),re.M):
        records[m.group(1)]=int(m.group(2))
    for name,original in items.items():
        frame=records.get(name)
        if frame is None or not(0<frame<=original):
            raise SystemExit(f'P1_RX_BUNDLE=ERROR invalid_frame={owner}:{name}:{frame}')
        total+=original-frame
        print(f'P1_RX_BUNDLE=FRAME owner={owner}.o function={name} base={original} candidate={frame} delta={original-frame}')
print(f'P1_RX_BUNDLE=MEASURED function_frame_savings_sum={total} candidate_not_certified=true')
PY_RX_BUNDLE
  if [[ "$rx_bundle_dev" == 1 ]]; then
    python3 - "$out/net/core/dev.minic-stage2.minic.stderr" <<'PY_DEV_AUDIT'
from pathlib import Path
import re,sys
p=Path(sys.argv[1])
assert p.is_file(),f"missing candidate dev.o stderr: {p}"
rows=dict((m.group(1),int(m.group(2))) for m in re.finditer(
    r'^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b',p.read_text(errors='replace'),re.M))
focus=['net_rx_action','process_backlog','__dev_queue_xmit']
for name in focus:
    print(f"P1_RX_BUNDLE_DEV=FRAME function={name} candidate_frame={rows.get(name,'missing')}")
assert sum(name in rows for name in focus)>=2,"missing receive/transmit focus"
PY_DEV_AUDIT
    if [[ "$rx_bundle_neigh" == 1 ]]; then
      # Fail before kernel relink if the new object does not buy meaningful
      # stack. Earlier same-profile __neigh_create measured 3024 B; the new
      # backend value must actually shrink by >=512 B, as emitted by MiniC.
      python3 - "$out/net/core/neighbour.minic-stage2.minic.stderr" "$dest" <<'PY_NEIGH_ROI'
from pathlib import Path
import re,sys
log,dest=Path(sys.argv[1]),Path(sys.argv[2])
if not log.is_file(): raise SystemExit("P1_RX_NEIGH=FAIL missing_new_owner_stderr")
contents=log.read_text(errors="replace")
pairs=dict((x.group(1),int(x.group(2))) for x in re.finditer(
    r'^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b',contents,re.M))
original=3024
frame=pairs.get("__neigh_create")
if frame is None or frame <= 0 or frame > original-512:
    raise SystemExit(f"P1_RX_NEIGH=NO_ROI baseline={original} candidate={frame} min_saving=512 abort_before_link=1")
(dest/"neighbour-frame.tsv").write_text(
    f"owner\tfunction\tbaseline_frame\tcandidate_frame\tsaving\nnet/core/neighbour.o\t__neigh_create\t{original}\t{frame}\t{original-frame}\n")
print(f"P1_RX_NEIGH=FRAME_PASS function=__neigh_create baseline={original} candidate={frame} saved={original-frame} original_objects_frozen=2056")
PY_NEIGH_ROI
    fi
    if [[ "$rx_bundle_timer" == 1 ]]; then
      python3 - "$out/kernel/time/hrtimer.minic-stage2.minic.stderr" <<'PY_TIMER'
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);assert p.is_file()
rows=dict((m.group(1),int(m.group(2))) for m in re.finditer(
  r'^CORE_FRAME_REUSE function=(\S+) frame=(\d+)\b',p.read_text(errors='replace'),re.M))
for name in ('hrtimer_interrupt','__hrtimer_run_queues','__run_hrtimer','hrtimer_run_softirq'):
  print(f"P1_RX_BUNDLE_TIMER=FRAME function={name} candidate={rows.get(name,'inlined_or_missing')}")
assert 'hrtimer_interrupt' in rows,'hrtimer IRQ target absent'
PY_TIMER
    fi
  fi
fi
if [[ "$rx_bundle_timer" == 1 ]]; then
  if ! command -v qemu-riscv64 >/dev/null 2>&1; then
    sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends qemu-user
  fi
  echo "P1_RX_BUNDLE_TIMER=SEMANTIC_START"
  MINIC="$minic" WORK="$dest/cfg-semantic" RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
    bash "$repo/tests/compiler/c0/run-core-cfg-slot-reuse-p1.sh"
  echo "P1_RX_BUNDLE_TIMER=SEMANTIC_PASS"
  # Stronger regression guard on the EXACT experimental compiler, before
  # compiling the 8 Linux owners and before any expensive GNU Image link.
  MINIC="$minic" WORK="$dest/cfg-adversarial" RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
    bash "$repo/tests/compiler/c0/run-core-cfg-slot-reuse-adversarial-p1.sh"
  echo "P1_RX_BUNDLE_TIMER=ADVERSARIAL_PASS cases=8 inputs=1023 baseline_and_GCC_differential=true"
  if [[ "${MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ:-0}" == 1 ]]; then
    MINIC="$minic" BUILD_DIR="$dest" RISCV_CC=riscv64-linux-gnu-gcc \
      QEMU_RISCV64=qemu-riscv64 \
      bash "$repo/tests/compiler/c0/run-pure-discarded-address-rv64.sh"
    echo "P1_COMBINED_DISCARD=DIFFERENTIAL_PASS gcc_old_new_qemu=3/3"
  fi
fi
export COHORT_P1_EXPERIMENTAL_POOL="$dest/pool/minic"
last_selection="none"
last_link_source_dir=""
last_candidate_delta=""
printf 'trial\timage_sha256\tverdict\n' >"$dest/p1-repeats.tsv"
pinned=""
passes=0
mode=prefix
if [[ "$single_owner" == 1 ]]; then
  mode=exclude_list
  printf '%s\n' net/ipv6/tcp_ipv6.o >"$ev/p1_gcc_rescue_set.txt"
  echo "P1_DEFINED_CROSSBLOCK=SINGLE_OWNER_ONLY minic_modified=net/ipv6/ip6_output.o gcc_owner=net/ipv6/tcp_ipv6.o"
fi
repeats=3
if [[ "$rx_bundle_dev" == 1 ]]; then repeats=6; fi
if [[ "$rx_bundle_timer" == 1 ]]; then
  repeats=12
  if git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-sched-chain]'; then
    repeats=3
    echo "P1_RX_BUNDLE_TIMER=SCHED_TRACE_ONLY repeats=3 cache=2064 source_producers=7 gdb=1"
  elif git -C "$repo" log -1 --format=%B | grep -Fq '[linux-runtime-p1-24boot-audit]'; then
    repeats=24
    echo "P1_RX_BUNDLE_TIMER=AUDIT_START boot_repeats=$repeats same_image=true immutable_minic_pool=true"
  fi
fi
for attempt in $(seq 1 "$repeats"); do
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
  gdb_seconds=65
  if [[ "${P1_GDB_SCHED_CHAIN:-0}" == 1 ]]; then gdb_seconds=120; fi
  if bash "$repo/tools/ci/linux-runtime-p1-gdb-stack-v1.sh" \
       "$experimental_image" "$out/vmlinux" \
       "$ev/runtime-initramfs.cpio.gz" "$LINUX_RUNTIME_CUSTOM_DTB" \
       "$ev/p1-gdb-stack-experiment" "$gdb_seconds"; then
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
# Report every normal boot, but attribute IRQ underflow ONLY for actual
# failing boots of this exact Image. Earlier logic quit at the first PASS,
# losing the rare 20/24 failures that motivated this eight-owner diagnosis.
repeats_file = report.parent / "p1-repeats.tsv"
records = [line.split("\t") for line in repeats_file.read_text().splitlines()[1:]]
if not records or any(len(row) != 3 or row[1] != expected_sha for row in records):
    raise SystemExit("P1_DEFINED_CROSSBLOCK_IRQ=ERROR invalid_same_image_trial_manifest")
rows = []
for raw_index, _, verdict in records:
    index = int(raw_index)
    if verdict == "PASS":
        rows.append((index, "PASS", "-", "-"))
        continue
    log = (ev / f"trials/p1_defined_crossblock_{index}/qemu/rdinit-init.log").read_text(errors="replace")
    crash = log.split("Insufficient stack space to handle exception!", 1)
    mm = re.search(r"\bsp\s*:\s*(?:0x)?([0-9a-fA-F]{16})", crash[-1])
    if len(crash) != 2 or not mm:
        rows.append((index, "NON_STACK_FAILURE", "-", "-"))
        continue
    fault_sp = int(mm.group(1), 16)
    rows.append((index, "STACK_OVERFLOW", f"0x{fault_sp:x}", str(base - fault_sp)))
report.write_text(
    "trial\tverdict\ttrapped_sp\tbytes_below_irq_stack_base\n" +
    "".join(f"{i}\t{verdict}\t{sp}\t{delta}\n"
            for i, verdict, sp, delta in rows)
)
failures = [row for row in rows if row[1] != "PASS"]
stack_failures = [row for row in failures if row[1] == "STACK_OVERFLOW"]
if not failures:
    print("P1_DEFINED_CROSSBLOCK_IRQ=NO_CRASH_OBSERVED "
          "same_image_sha=%s attempts=%d gdb_timing_perturbed=true"
          % (expected_sha, len(rows)))
elif len(stack_failures) == len(failures) and all(
        0 < int(row[3]) <= 4096 for row in stack_failures):
    print("P1_DEFINED_CROSSBLOCK_IRQ=VERIFIED irq_base=0x%x irq_top=0x%x "
          "trapped_sp_below_base_bytes=%s failures=%d attempts=%d "
          "same_image_sha=%s debugger_not_a_P1_oracle=true" %
          (base, top, ",".join(row[3] for row in stack_failures),
           len(failures), len(rows), expected_sha))
else:
    print("P1_DEFINED_CROSSBLOCK_IRQ=INCONCLUSIVE irq_base=0x%x "
          "failures=%d stack_failures=%d attempts=%d"
          % (base, len(failures), len(stack_failures), len(rows)))
PY_IRQ_ATTRIBUTION
  else
    echo "P1_DEFINED_CROSSBLOCK_GDB=INCONCLUSIVE no_P1_pass_inference=true"
  fi
fi
# Previous diagnostics always exited zero so engineers could inspect partial
# progress (0/3, 1/3, 2/3). The mature 7-owner 16KiB acceptance is different:
# publish full evidence, then fail the job if even one actual P1 run failed.
# Do not allow a green job to be mistaken for 12/12 guest certification.
if [[ "$rx_bundle_timer" == 1 ]]; then
  echo "P1_RX_BUNDLE_TIMER=COMPLETE owners=$((7+rx_bundle_neigh)) frozen_minic=$((2057-rx_bundle_neigh)) gcc_owners=0 passes=$passes failures=$((repeats-passes)) attempts=$repeats image_sha=$pinned experimental_not_promoted=true"
  if [[ "$passes" -ne "$repeats" ]]; then
    echo "P1_RX_BUNDLE_TIMER=FAIL require_all_P1_PASS actual=$passes expected=$repeats image_sha=$pinned" >&2
    exit 8
  fi
  echo "P1_RX_BUNDLE_TIMER=ACCEPTED owners=$((7+rx_bundle_neigh)) p1=$passes/$repeats same_image_sha=$pinned experimental_not_promoted=true"
elif [[ "$rx_bundle_dev" == 1 ]]; then
  echo "P1_RX_BUNDLE_DEV=COMPLETE owners=6 frozen_minic=2058 gcc_owners=0 passes=$passes failures=$((repeats-passes)) attempts=$repeats image_sha=$pinned experimental_not_promoted=true"
elif [[ "$rx_bundle_page" == 1 ]]; then
  echo "P1_RX_BUNDLE_PAGE=COMPLETE modified_minic=5 unchanged_minic=2059 gcc_owners=0 passes=$passes failures=$((3-passes)) image_sha=$pinned candidate_not_promoted=true"
elif [[ "$rx_bundle" == 1 ]]; then
  echo "P1_RX_BUNDLE=COMPLETE modified_minic=4 unchanged_minic=2060 gcc_owners=0 passes=$passes failures=$((3-passes)) image_sha=$pinned candidate_not_promoted=true"
elif [[ "$single_owner" == 1 ]]; then
  echo "P1_DEFINED_CROSSBLOCK=COMPLETE modified_minic=1 gcc_owners=1 minic_owners=2063 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
else
  echo "P1_DEFINED_CROSSBLOCK=COMPLETE modified_minic=4 gcc_owners=0 minic_owners=2064 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
fi
exit 0

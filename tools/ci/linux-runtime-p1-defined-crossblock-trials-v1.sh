#!/usr/bin/env bash
# Experimental opt-in: exactly four MiniC TU recompiles with a conservative
# first-write cross-basic-block reuse proof. No mutation of the SHA-certified
# 2064-object pool; full GNU provenance and same-Image P1 boots are mandatory.
set -Eeuo pipefail
[[ -n "$minic" && -n "$gold_snapshot" && -n "$ev" && -n "$out" ]] || exit 8
[[ "$n" -eq 2064 ]] || { echo "P1_DEFINED_CROSSBLOCK=ERROR object_count"; exit 8; }
dest="$ev/p1-defined-crossblock"
mkdir -p "$dest"
cat >"$dest/selected.txt" <<'OWNERS'
net/ipv6/ip6_output.o
net/ipv6/tcp_ipv6.o
net/ipv4/tcp_output.o
net/core/dev.o
OWNERS
python3 - "$dest/selected.txt" "$objects_file" <<'PY_SET'
from pathlib import Path
import sys
a,b=map(Path,sys.argv[1:])
names=a.read_text().splitlines()
assert len(names)==4 and len(set(names))==4
assert set(names)<=set(b.read_text().splitlines())
print("P1_DEFINED_CROSSBLOCK=SELECTED owners=4")
PY_SET
(cd "$ev" && sha256sum --quiet -c minic-manifest.sha256)
printf 'compiler_sha256=%s\n' "$(sha256sum "$minic"|cut -d' ' -f1)" >"$dest/provenance.txt"
rm -rf -- "$out"
cp -a --reflink=auto "$gold_snapshot" "$out"
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
assert matched==4 and untouched==2060
print("P1_DEFINED_CROSSBLOCK=POOL_ISOLATED changed_targets=4 original_neighbors=2060")
PY_VERIFY
export COHORT_P1_EXPERIMENTAL_POOL="$dest/pool/minic"
last_selection="none"
last_link_source_dir=""
last_candidate_delta=""
printf 'trial\timage_sha256\tverdict\n' >"$dest/p1-repeats.tsv"
pinned=""
passes=0
for attempt in 1 2 3; do
  trial "p1_defined_crossblock_$attempt" "$n" p1
  [[ -n "$pinned" ]] || pinned="$image_sha"
  [[ "$image_sha" == "$pinned" ]] || {
    echo "P1_DEFINED_CROSSBLOCK=ERROR image_sha_drift"; exit 8;
  }
  printf '%s\t%s\t%s\n' "$attempt" "$image_sha" "$TRIAL_VERDICT" >>"$dest/p1-repeats.tsv"
  if [[ "$TRIAL_VERDICT" == PASS ]]; then passes=$((passes+1)); fi
done
echo "P1_DEFINED_CROSSBLOCK=COMPLETE candidate_owners=4 gcc_owners=0 minic_owners=2064 passes=$passes failures=$((3-passes)) image=$pinned experimental=true not_promoted=true"
exit 0

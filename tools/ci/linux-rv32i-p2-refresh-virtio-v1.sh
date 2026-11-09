#!/usr/bin/env bash
# RV32 P2: retain 1738 SHA-attested frozen MiniC Linux C owners; replace
# the three exact virtio core/PCI-transport source owners with current MiniC
# under the original Kbuild wrapper, then require genuine QEMU feature32.
# The full 18/18 genuine QEMU test remains mandatory and fail-closed.
set -Eeuo pipefail
: "$GITHUB_WORKSPACE"
root="$GITHUB_WORKSPACE/build/rv32-p2-strict"
src="$root/linux-6.6.143"
golden="$GITHUB_WORKSPACE/build/rv32i-linux-entry"
out="$golden/out"
prov="$golden/provenance"
shards="$root/shards"
ev="$root/refresh-virtio"
mkdir -p "$ev"
test -s "$src/Makefile"
test -s "$prov/full-c-objects.txt"
old_sha=a762cb8ba55a3cbb52a1f85e61d40524941c8226a33cb35daf2bff1acd75a5a2
test "$(sha256sum "$root/source/out/arch/riscv/boot/Image" | cut -d' ' -f1)" = "$old_sha"
test "$(sha256sum "$out/.config" | cut -d' ' -f1)" = 4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c
test "$(sha256sum "$(command -v riscv64-linux-gnu-gcc)" | cut -c1-16)" = 0c2a199ea3ae14d3
test "$(sha256sum "$(command -v riscv64-linux-gnu-ld)" | cut -c1-16)" = 0703113036a30806
test "$(sha256sum "$(command -v riscv64-linux-gnu-as)" | cut -c1-16)" = d072447ef15a2074
grep -Fxq 'elf_class=ELF32' "$prov/gcc-baseline.txt"
grep -Fxq 'isa=rv32i' "$prov/gcc-baseline.txt"
grep -Fxq 'abi=ilp32' "$prov/gcc-baseline.txt"
(cd "$out" && sha256sum --quiet -c "$prov/gcc-objects.sha256")
echo 'RV32I_P2_REFRESH_BASELINE=PASS exact_original_GNU_1741=true original_Image_sha=true'
# First settle generated dependencies before installing any MiniC C owner.
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- KBUILD_NOCMDDEP=1 -j4 prepare \
  >"$ev/prepare.log" 2>&1 || { tail -n 70 "$ev/prepare.log" >&2; exit 4; }
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- KBUILD_NOCMDDEP=1 -j4 Image \
  >"$ev/gnu-reconcile.log" 2>&1 || { tail -n 70 "$ev/gnu-reconcile.log" >&2; exit 4; }
test "$(sha256sum "$out/.config" | cut -d' ' -f1)" = 4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$ev/minic-build" all \
  >"$ev/minic-build.log" 2>&1 || { tail -n 70 "$ev/minic-build.log" >&2; exit 4; }
test -x "$ev/minic-build/bin/minic-cc"
# Three strictly scoped C owners, each recompiled through the actual Linux
# stage2 Kbuild wrapper (not a handwritten GNU command). The historical
# MiniC vp_finalize_features() in virtio_pci_modern.c falsely rejected
# VIRTIO_F_VERSION_1 while the live device sysfs bit32 was already set.
for owner in virtio virtio_pci_modern_dev virtio_pci_modern; do
  sha256sum "$src/drivers/virtio/$owner.c" >"$ev/$owner-original-source.sha256"
  touch "$src/drivers/virtio/$owner.c"
  MINIC="$ev/minic-build/bin/minic-cc" REAL_CC=riscv64-linux-gnu-gcc \
    MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 MINIC_KEEP_INTERMEDIATES=1 \
    MINIC_KBUILD_TRACE="$ev/$owner-kbuild-route" \
    MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/$owner-object-hashes" \
    MINIC_KBUILD_SUCCESS_TRACE="$ev/$owner-success-owners" \
    make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
      CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" KBUILD_NOCMDDEP=1 \
      -j1 "drivers/virtio/$owner.o" >"$ev/$owner-one-owner.log" 2>&1 || {
        echo "RV32I_P2_REFRESH=FAIL current_MiniC_Kbuild_owner=$owner" >&2
        tail -n 80 "$ev/$owner-one-owner.log" >&2; exit 4;
      }
  test "$(wc -l <"$ev/$owner-object-hashes")" -eq 1
  test "$(wc -l <"$ev/$owner-success-owners")" -eq 1
  grep -Fq "drivers/virtio/$owner.o" "$ev/$owner-success-owners"
  grep -Fq 'minic rv32i_origin_audit=1' "$ev/$owner-kbuild-route"
  test -s "$out/drivers/virtio/$owner.minic-stage2.i"
  test -s "$out/drivers/virtio/$owner.minic-stage2.s"
  cp "$out/drivers/virtio/$owner.o" "$ev/current-$owner.o"
  riscv64-linux-gnu-readelf -h "$ev/current-$owner.o" | grep -Eq 'Class:[[:space:]]+ELF32'
  echo "RV32I_P2_REFRESH_OWNER=PASS owner=$owner single_MiniC_C=true GNU_AS=true"
done
# Audit frozen shard inventory 1741/1741, retain 1738 byte-identical,
# replace exactly three proven virtio feature/finalizer owners.
python3 - "$prov/full-c-objects.txt" "$shards" "$out" "$ev/current-virtio.o" "$ev/current-virtio_pci_modern_dev.o" "$ev/current-virtio_pci_modern.o" "$ev/candidate-hashes.sha256" <<'PY_AUDIT'
from pathlib import Path
import hashlib, re, shutil, sys
listing, shards, out, current_core, current_pci_dev, current_pci_finalizer, hashes = map(Path, sys.argv[1:])
owners = [s for s in listing.read_text().splitlines() if s and not s.startswith('#')]
assert len(owners) == 1741 and len(set(owners)) == 1741
observed = {}
for i in range(7):
    folder = shards / ('shard-%d' % i)
    names = [n for n in (folder/'full-objects.txt').read_text().splitlines()
             if n and not n.startswith('#')]
    assert names == owners[i::7], (i, len(names))
    manifest = {}
    for line in (folder/'minic-manifest.sha256').read_text().splitlines():
        m = re.fullmatch(r'([0-9a-f]{64})  minic/(.+\.o)', line)
        assert m, line[:120]
        manifest[m[2]] = m[1]
    assert set(manifest) == set(names)
    for name in names:
        p = folder/'minic'/name
        assert p.is_file() and (out/name).is_file(), name
        assert hashlib.sha256(p.read_bytes()).hexdigest() == manifest[name], name
        observed[name] = (p, manifest[name])
changed = {
    'drivers/virtio/virtio.o': current_core,
    'drivers/virtio/virtio_pci_modern_dev.o': current_pci_dev,
    'drivers/virtio/virtio_pci_modern.o': current_pci_finalizer,
}
new_digests = {name: hashlib.sha256(p.read_bytes()).hexdigest()
               for name, p in changed.items()}
for name, digest in new_digests.items():
    assert observed[name][1] != digest, 'frozen MiniC owner remains unchanged: '+name
for name in owners:
    # copyfile deliberately updates mtime: Kbuild must never recompile this
    # SHA-attested C owner in the following *link-only* phase.
    shutil.copyfile(changed[name] if name in changed else observed[name][0], out/name)
with hashes.open('w') as f:
    for name in owners:
        h = new_digests[name] if name in new_digests else observed[name][1]
        f.write('%s  %s\n' % (h, name))
print('RV32I_P2_REFRESH_OWNERS=PASS C_owners=1741/1741 frozen=1738 current_virtio=3 all_SHA=true')
PY_AUDIT
# Read-only machine-code proof for the exact old/current finalizer that
# tests __virtio_test_bit(vdev, VIRTIO_F_VERSION_1) == BIT_ULL(32).
# Symbol may be folded/renamed; record absence honestly, never substitute
# disassembly inspection for the real GPU QEMU oracle.
slot=$(python3 - "$prov/full-c-objects.txt" <<'PY_SLOT'
from pathlib import Path
import sys
owners = [s for s in Path(sys.argv[1]).read_text().splitlines()
          if s and not s.startswith('#')]
assert len(owners) == 1741
print(owners.index('drivers/virtio/virtio_pci_modern.o') % 7)
PY_SLOT
)
frozen_finalizer="$shards/shard-$slot/minic/drivers/virtio/virtio_pci_modern.o"
current_finalizer="$ev/current-virtio_pci_modern.o"
for variant in frozen current; do
  if [[ "$variant" == frozen ]]; then candidate="$frozen_finalizer";
  else candidate="$current_finalizer"; fi
  test -s "$candidate"
  if riscv64-linux-gnu-readelf -Ws "$candidate" |
      grep -Eq '[[:space:]]vp_finalize_features([[:space:]]|$)'; then
    echo "RV32I_P2_FINALIZER_DISASM=AVAILABLE variant=$variant" |
      tee "$ev/$variant-finalizer-symbol.txt"
    riscv64-linux-gnu-objdump -dr --disassemble=vp_finalize_features \
      "$candidate" >"$ev/$variant-finalizer.dis" || true
  else
    echo "RV32I_P2_FINALIZER_DISASM=UNAVAILABLE variant=$variant symbol=absent" |
      tee "$ev/$variant-finalizer-symbol.txt"
    riscv64-linux-gnu-objdump -dr "$candidate" \
      >"$ev/$variant-finalizer-full.dis" || true
  fi
done
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$ev/runtime" \
  >"$ev/runtime-build.log" 2>&1 || { tail -n 70 "$ev/runtime-build.log" >&2; exit 4; }
test "$(wc -l <"$ev/runtime/rv32-runtime-objects.rsp")" -ge 17
mapfile -t runtime_objects <"$ev/runtime/rv32-runtime-objects.rsp"
riscv64-linux-gnu-ar rcsD "$ev/softarith.a" "${runtime_objects[@]}"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  KBUILD_NOCMDDEP=1 -j4 --eval="vmlinux: KBUILD_VMLINUX_LIBS += $ev/softarith.a" \
  Image >"$ev/gnu-relink.log" 2>&1 || {
    echo 'RV32I_P2_REFRESH=FAIL GNU_relink' >&2
    tail -n 80 "$ev/gnu-relink.log" >&2; exit 4;
  }
# Linux's generated-header closure can cause a bounded set of GNU owners to
# be recompiled during the first link despite their earlier byte-attested
# replacement. Restore only those exact previously certified MiniC shard bytes
# AFTER the generated dependencies settle, then re-run GNU link. The original
# seven-shard cohort helper did the same preparation/byte reconciliation.
# Never forgive a changed virtio object or an unexpected non-header owner.
total_rebuilt=$(grep -Ec '^[[:space:]]+CC[[:space:]]+' "$ev/gnu-relink.log" || true)
for round in 1 2; do
  if (cd "$out" && sha256sum --quiet -c "$ev/candidate-hashes.sha256") \
       >"$ev/pre-repair-$round.audit" 2>&1; then
    echo "RV32I_P2_REFRESH_RECONCILE=PASS round=$round no_owner_drift=true"
    break
  fi
  python3 - "$prov/full-c-objects.txt" "$shards" "$out" "$ev/candidate-hashes.sha256" <<'PY_RECONCILE'
from pathlib import Path
import hashlib, os, shutil, sys
owners_file, shards, out, expected_file = map(Path, sys.argv[1:])
owners = [s for s in owners_file.read_text().splitlines()
          if s and not s.startswith('#')]
sha = {}
for line in expected_file.read_text().splitlines():
    digest, name = line.split('  ', 1)
    sha[name] = digest
assert len(owners) == len(sha) == 1741
allowed = {'arch/riscv/kernel/alternative.o',
           'arch/riscv/kernel/signal.o',
           'arch/riscv/kernel/vdso.o'}
drift = []
for pos, owner in enumerate(owners):
    actual = hashlib.sha256((out/owner).read_bytes()).hexdigest()
    if actual == sha[owner]:
        continue
    assert owner in allowed, 'unapproved MiniC C owner drift: '+owner
    source = shards/('shard-%d' % (pos % 7))/'minic'/owner
    assert hashlib.sha256(source.read_bytes()).hexdigest() == sha[owner], owner
    # copyfile + utime makes the restored object newer than the just-settled
    # generated header, so GNU linker only sees the correct immutable bytes.
    shutil.copyfile(source, out/owner)
    os.utime(out/owner, None)
    drift.append(owner)
assert drift, 'sha mismatch with empty owner-drift inventory'
print('RV32I_P2_REFRESH_RECONCILE=RETRY affected='+','.join(drift))
PY_RECONCILE
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    KBUILD_NOCMDDEP=1 -j1 --eval="vmlinux: KBUILD_VMLINUX_LIBS += $ev/softarith.a" \
    Image >"$ev/gnu-relink-repair-$round.log" 2>&1 || {
      echo "RV32I_P2_REFRESH=FAIL GNU_relink_repair_round=$round" >&2
      tail -n 80 "$ev/gnu-relink-repair-$round.log" >&2
      exit 4
    }
  count=$(grep -Ec '^[[:space:]]+CC[[:space:]]+' "$ev/gnu-relink-repair-$round.log" || true)
  total_rebuilt=$((total_rebuilt + count))
  if ((total_rebuilt > 5)); then
    echo "RV32I_P2_REFRESH=FAIL unexpected_GNU_C_recompiles=$total_rebuilt" >&2
    exit 4
  fi
done
(cd "$out" && sha256sum --quiet -c "$ev/candidate-hashes.sha256") || {
  echo 'RV32I_P2_REFRESH=FAIL post_link_1741_owner_SHA_drift' >&2; exit 4;
}
rebuilt=$total_rebuilt
test "$rebuilt" -le 5 || { echo "RV32I_P2_REFRESH=FAIL unexpected_rebuild=$rebuilt" >&2; exit 4; }
new_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
test -n "$new_sha"
test "$new_sha" != "$old_sha"
printf 'original_Image_sha256=%s\ncurrent_Image_sha256=%s\nMiniC_C_owners=1738_frozen_plus_3_current\nGNU_link_rebuilt_C=%s\n' \
  "$old_sha" "$new_sha" "$rebuilt" >"$ev/provenance.txt"
echo "RV32I_P2_REFRESH=PASS new_Image_sha256=$new_sha old_Image_sha256=$old_sha MiniC_C=1741/1741 GNU_link=true"

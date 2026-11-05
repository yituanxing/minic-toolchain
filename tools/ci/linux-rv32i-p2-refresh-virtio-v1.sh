#!/usr/bin/env bash
# RV32 P2: retain 1740 SHA-attested frozen MiniC Linux C owners; replace only
# drivers/virtio/virtio.c using current MiniC + original Kbuild wrapper.
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
sha256sum "$src/drivers/virtio/virtio.c" >"$ev/virtio-original-source.sha256"
# Original wrapper, original Linux source, original -f flags, single C TU.
touch "$src/drivers/virtio/virtio.c"
MINIC="$ev/minic-build/bin/minic-cc" REAL_CC=riscv64-linux-gnu-gcc \
  MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 MINIC_KEEP_INTERMEDIATES=1 \
  MINIC_KBUILD_TRACE="$ev/kbuild-route" \
  MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/owner-object-hashes" \
  MINIC_KBUILD_SUCCESS_TRACE="$ev/success-owners" \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" KBUILD_NOCMDDEP=1 \
    -j1 drivers/virtio/virtio.o >"$ev/one-owner.log" 2>&1 || {
      echo 'RV32I_P2_REFRESH=FAIL current_virtio_MiniC_Kbuild' >&2
      tail -n 80 "$ev/one-owner.log" >&2; exit 4;
    }
test "$(wc -l <"$ev/owner-object-hashes")" -eq 1
test "$(wc -l <"$ev/success-owners")" -eq 1
grep -Fq 'drivers/virtio/virtio.o' "$ev/success-owners"
grep -Fq 'minic rv32i_origin_audit=1' "$ev/kbuild-route"
test -s "$out/drivers/virtio/virtio.minic-stage2.i"
test -s "$out/drivers/virtio/virtio.minic-stage2.s"
cp "$out/drivers/virtio/virtio.o" "$ev/current-virtio.o"
riscv64-linux-gnu-readelf -h "$ev/current-virtio.o" | grep -Eq 'Class:[[:space:]]+ELF32'
# Audit frozen shard inventory 1741/1741, retain 1740 byte-identical,
# replace only one broken October 31 object with current verified MiniC.
python3 - "$prov/full-c-objects.txt" "$shards" "$out" "$ev/current-virtio.o" "$ev/candidate-hashes.sha256" <<'PY_AUDIT'
from pathlib import Path
import hashlib, re, shutil, sys
listing, shards, out, current, hashes = map(Path, sys.argv[1:])
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
changed = 'drivers/virtio/virtio.o'
new_digest = hashlib.sha256(current.read_bytes()).hexdigest()
assert observed[changed][1] != new_digest, 'frozen MiniC virtio remains unchanged'
for name in owners:
    # copyfile deliberately updates mtime: Kbuild must never recompile this
    # SHA-attested C owner in the following *link-only* phase.
    shutil.copyfile(current if name == changed else observed[name][0], out/name)
with hashes.open('w') as f:
    for name in owners:
        h = new_digest if name == changed else observed[name][1]
        f.write('%s  %s\n' % (h, name))
print('RV32I_P2_REFRESH_OWNERS=PASS C_owners=1741/1741 frozen=1740 current_virtio=1 all_SHA=true')
PY_AUDIT
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
(cd "$out" && sha256sum --quiet -c "$ev/candidate-hashes.sha256") || {
  echo 'RV32I_P2_REFRESH=FAIL unexpected_C_owner_recompiled' >&2; exit 4;
}
rebuilt=$(grep -Ec '^[[:space:]]+CC[[:space:]]+' "$ev/gnu-relink.log" || true)
test "$rebuilt" -le 5 || { echo "RV32I_P2_REFRESH=FAIL unexpected_rebuild=$rebuilt" >&2; exit 4; }
new_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
test -n "$new_sha"
test "$new_sha" != "$old_sha"
printf 'original_Image_sha256=%s\ncurrent_Image_sha256=%s\nMiniC_C_owners=1740_frozen_plus_1_current\nGNU_link_rebuilt_C=%s\n' \
  "$old_sha" "$new_sha" "$rebuilt" >"$ev/provenance.txt"
echo "RV32I_P2_REFRESH=PASS new_Image_sha256=$new_sha old_Image_sha256=$old_sha MiniC_C=1741/1741 GNU_link=true"

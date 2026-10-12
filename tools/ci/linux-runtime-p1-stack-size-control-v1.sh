#!/usr/bin/env bash
# Controlled Linux 6.6.143 RISC-V 16KiB -> 32KiB task+IRQ stack experiment.
# No MiniC codegen edits; build a NEW exact-config GNU reference and recompile
# all 2064 corresponding MiniC C owners. Never link stale 16KiB objects.
set -Eeuo pipefail
if (( $# != 4 )); then
  echo "usage: $0 <linux-src> <certified-16k-gnu-out> <certified-16k-provenance> <workdir>" >&2
  exit 64
fi
src=$(realpath "$1")
old=$(realpath "$2")
old_prov=$(realpath "$3")
work=$(realpath -m "$4")
repo=$(cd "$(dirname "$0")/../.." && pwd)
ev="$work/evidence"
out="$work/out32"
toolchain="$work/minic-profile"
mkdir -p "$ev"
exec > >(tee "$ev/stack-size-control.log") 2>&1
echo "P1_STACK32_GOAL=CAPACITY_CAUSAL_CONTROL no_compiler_codegen_edits=true desired_task_stack=32768 desired_irq_stack=32768 same_2064_owners_required=true"
trap 'rc=$?; echo "P1_STACK32_UNHANDLED rc=$rc line=$LINENO cmd=$BASH_COMMAND" >&2' ERR

test -s "$old/.config" && test -s "$old/arch/riscv/boot/Image"
test -s "$old_prov/gcc-objects.sha256"
test -s "$src/arch/riscv/Kconfig"
test "$(sed -n 's/^CONFIG_THREAD_SIZE_ORDER=//p' "$old/.config")" = 2
grep -Fqx 'CONFIG_VMAP_STACK=y' "$old/.config"
grep -Fqx 'CONFIG_IRQ_STACKS=y' "$old/.config"
! grep -Fqx 'CONFIG_KASAN=y' "$old/.config" || exit 8
printf 'P1_STACK32_BASELINE config=%s image=%s source=6.6.143\n' \
  "$(sha256sum "$old/.config" | cut -d' ' -f1)" \
  "$(sha256sum "$old/arch/riscv/boot/Image" | cut -d' ' -f1)"

# Do not let old .o artefacts into the experimental Image without Kbuild's
# own exact .config/.cmd dependency tracking; the final 32KiB reference is
# compiled before the complete new MiniC object pool is created.
cp -a --reflink=auto "$old" "$out"
cp "$old/.config" "$ev/config-16k"
kconfig="$src/arch/riscv/Kconfig"
cp "$kconfig" "$ev/riscv-Kconfig-original"
python3 - "$kconfig" <<'PY_KCONFIG'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
anchor='config THREAD_SIZE_ORDER\n\tint "Kernel stack size (in power-of-two numbers of page size)" if VMAP_STACK && EXPERT\n\trange 0 4\n\tdefault 1 if 32BIT\n\tdefault 2\n'
assert s.count(anchor)==1, "Linux 6.6.143 Kconfig structure unexpected"
p.write_text(s.replace(anchor,anchor.replace("\tdefault 2\n","\tdefault 3\n")))
print("P1_STACK32_KCONFIG=PATCHED only_thread_size_default=2_to_3")
PY_KCONFIG
"$src/scripts/config" --file "$out/.config" --set-val THREAD_SIZE_ORDER 3
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
python3 - "$ev/config-16k" "$out/.config" "$ev/config-delta.tsv" <<'PY_CONFIG'
from pathlib import Path
import re,sys
before,after,dest=(Path(x) for x in sys.argv[1:])
pat=re.compile(r'^(?:CONFIG_(\w+)=(.*)|# CONFIG_(\w+) is not set)$')
def parse(p):
    result={}
    for line in p.read_text().splitlines():
        m=pat.fullmatch(line)
        if m:
            result[m.group(1) or m.group(3)] = m.group(2) if m.group(1) else "n"
    return result
a,b=parse(before),parse(after)
dif=[(name,a.get(name,"ABSENT"),b.get(name,"ABSENT"))
     for name in sorted(a.keys()|b.keys()) if a.get(name)!=b.get(name)]
dest.write_text("option\told\tnew\n"+"".join("%s\t%s\t%s\n"%x for x in dif))
assert dif==[("THREAD_SIZE_ORDER","2","3")], "Non-isolated Kconfig delta: "+repr(dif[:25])
assert b.get("VMAP_STACK")=="y" and b.get("IRQ_STACKS")=="y"
assert b.get("KASAN")!="y"
print("P1_STACK32_CONFIG=ISOLATED order_16k=2 order_32k=3 task_and_irq_32k=true other_options_unchanged=true")
PY_CONFIG
grep -Fqx 'CONFIG_THREAD_SIZE_ORDER=3' "$out/include/config/auto.conf" || {
  # auto.conf is generated when the first Kbuild compilation starts.
  echo "P1_STACK32_CONFIG=OLDDEFCONFIG_OK auto.conf_pending=true"
}

# Positive control: GCC using the complete 32KiB configuration.
echo "P1_STACK32_GNU=BUILD_START"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  -j6 Image >"$ev/gcc32-build.log" 2>&1 || {
  echo "P1_STACK32_GNU=BUILD_FAIL"; tail -n 90 "$ev/gcc32-build.log"; exit 8;
}
grep -Fqx 'CONFIG_THREAD_SIZE_ORDER=3' "$out/include/config/auto.conf"
test -s "$out/vmlinux" && test -s "$out/arch/riscv/boot/Image"
echo "P1_STACK32_GNU=IMAGE_BUILT image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)"

# A fresh fixture must match the exact original 2064 C objects (no hidden
# extra C owners from configuration changes). Object hashes are keyed to 32KiB.
mkdir -p "$ev/provenance"
python3 - "$out" "$ev/provenance/gcc-objects.sha256" <<'PY_MANIFEST'
from pathlib import Path
import hashlib,struct,sys
out,dest=map(Path,sys.argv[1:])
with dest.open("w") as manifest:
    for obj in sorted(out.rglob("*.o")):
        header=obj.open("rb").read(20)
        if (len(header)<20 or header[:4]!=b"\x7fELF" or header[5]!=1 or
            struct.unpack_from("<H",header,16)[0]!=1 or
            struct.unpack_from("<H",header,18)[0]!=243):
            continue
        manifest.write(f"{hashlib.sha256(obj.read_bytes()).hexdigest()}  {obj.relative_to(out)}\n")
PY_MANIFEST
python3 "$repo/tools/ci/linux-runtime-gcc-full-c-objects-v1.py" \
  --out "$out" --manifest "$ev/provenance/gcc-objects.sha256" \
  --prefix "$repo/tools/ci/linux-runtime-gcc-cohort-objects-v1.txt" \
  --output "$ev/full-objects.txt" --excluded "$ev/non-c-objects.tsv" \
  --min-c-objects 2064
python3 - "$ev/full-objects.txt" <<'PY_COUNT'
from pathlib import Path
import sys
a=[x for x in Path(sys.argv[1]).read_text().splitlines() if x and not x.startswith("#")]
assert len(a)==2064, f"Different exact C object universe: {len(a)}"
print("P1_STACK32_OWNER_MANIFEST=PASS minic_c_owners=2064 new_config=true")
PY_COUNT
cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
image_sha=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)
{
  echo "schema=linux-gnu-exact-config-v1"
  echo "linux_release=6.6.143"
  echo "config_sha256=$cfg"
  echo "image_sha256=$image_sha"
  echo "qemu_contract=p1_positive_control_pending"
} >"$ev/provenance/gcc-baseline.txt"

BUILD_DIR="$ev/initramfs" OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" \
  >"$ev/initramfs-build.log" 2>&1 || {
  echo "P1_STACK32_GNU=INITRAMFS_BUILD_FAIL"; tail -n 60 "$ev/initramfs-build.log"; exit 8;
}
test -s "$ev/runtime-initramfs.cpio.gz"
# QEMU 8.2 virt's default -initrd physical placement overlaps ~215MB
# Linux Images even with -m 2G. Use the EXACT already-certified large
# Image DTB relocation contract for the 32KiB GNU positive control.
# Leaving this unset makes the otherwise complete experiment fail for a
# QEMU fixture reason before testing MiniC.
dtb="$ev/gnu-p1-virt.dtb"
qemu-system-riscv64 -M "virt,dumpdtb=$dtb" -cpu max \
  -m 2G -smp 1 -nographic -no-reboot -bios default \
  -kernel "$out/arch/riscv/boot/Image" \
  >"$ev/gnu-p1-dumpdtb.log" 2>&1
test -s "$dtb"
command -v fdtput >/dev/null
command -v fdtget >/dev/null
fdtget -p "$dtb" /chosen >/dev/null 2>&1 || fdtput -c "$dtb" /chosen
image_size=$(stat -c %s "$out/arch/riscv/boot/Image")
initrd_size=$(stat -c %s "$ev/runtime-initramfs.cpio.gz")
load_address=$((0xa0000000))
load_end=$((load_address+initrd_size))
kernel_end=$((0x80200000+image_size))
((load_address>=kernel_end && load_end < 0x100000000)) || {
  echo "P1_STACK32_GNU=BAD_DTB_GUEST_ADDRESS kernel_end=$kernel_end initrd_start=$load_address" >&2
  exit 8
}
printf -v load_end_hex '%x' "$load_end"
fdtput -t x "$dtb" /chosen linux,initrd-start 0 a0000000
fdtput -t x "$dtb" /chosen linux,initrd-end 0 "$load_end_hex"
[[ "$(fdtget -t x "$dtb" /chosen linux,initrd-start)" == "0 a0000000" ]] || exit 8
[[ "$(fdtget -t x "$dtb" /chosen linux,initrd-end)" == "0 $load_end_hex" ]] || exit 8
export LINUX_RUNTIME_CUSTOM_DTB="$dtb"
export LINUX_RUNTIME_INITRD_LOAD_ADDR=0xa0000000
echo "P1_STACK32_GNU_DTB=PASS initrd_at=0xa0000000 image_size=$image_size qemu_8_2_loader_workaround=true"
gnu_rc=0
LINUX_IMAGE="$out/arch/riscv/boot/Image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$ev/gnu-p1" LINUX_RELEASE=6.6.143 LINUX_RUNTIME_PROFILE=p1 \
  QEMU_TIMEOUT_SECONDS=90 LINUX_RUNTIME_QEMU_MEMORY=2G \
  bash "$repo/tests/external/linux/runtime_boot.sh" \
  >"$ev/gnu-p1.log" 2>&1 || gnu_rc=$?
if ((gnu_rc!=0)); then
  echo "P1_STACK32_GNU=INCONCLUSIVE_REFERENCE_P1_FAIL rc=$gnu_rc"
  tail -n 70 "$ev/gnu-p1.log"
  exit 8
fi
echo "P1_STACK32_GNU=PASS p1_positive_control=true stack_order=3"
sed -i 's/^qemu_contract=.*/qemu_contract=p1_passed/' "$ev/provenance/gcc-baseline.txt"

# Exact compiler source profile is pinned and unaffected by the 32KiB config.
bash "$repo/tools/ci/linux-runtime-build-minic-profile-v1.sh" \
  "$toolchain" "$ev/compiler-profile" >"$ev/minic-build.log" 2>&1 || {
  echo "P1_STACK32_MINIC=PROFILE_BUILD_FAIL"; tail -n 70 "$ev/minic-build.log"; exit 8;
}
test -x "$toolchain/bin/minic"
echo "P1_STACK32_MINIC=PROFILE_PASS compiler_sha=$(sha256sum "$toolchain/bin/minic" | cut -d' ' -f1)"
COHORT_ACTION=compile \
  bash "$repo/tools/ci/linux-runtime-gcc-cohort-bisect-v1.sh" \
  "$src" "$out" "$toolchain/bin/minic" "$ev/provenance" "$ev/full-objects.txt" "$ev" \
  >"$ev/compile-phase-output.log" 2>&1 || {
  echo "P1_STACK32_MINIC=COMPILE_FAIL"; tail -n 100 "$ev/compile-phase-output.log"; exit 8;
}
echo "P1_STACK32_MINIC=FULL_COMPILE_PASS"
# Reusing only the pinned 32KiB cache. Three ordinary QEMU P1 boots;
# diagnostic GDB is disabled so timing is not perturbed.
COHORT_ACTION=verify COHORT_P1_STACK_OWNER_SWAPS=1 \
COHORT_P1_STACK_SIZE_ONLY=1 COHORT_P1_GDB_STACK=0 \
  bash "$repo/tools/ci/linux-runtime-gcc-cohort-bisect-v1.sh" \
  "$src" "$out" "$toolchain/bin/minic" "$ev/provenance" "$ev/full-objects.txt" "$ev" \
  >"$ev/p1-phase-output.log" 2>&1 || {
  echo "P1_STACK32_MINIC=VERIFY_DIAGNOSTIC_ERROR"; tail -n 100 "$ev/p1-phase-output.log"; exit 8;
}
grep 'COHORT_P1_STACK32=RESULT' "$ev/p1-phase-output.log" | tail -n 1
echo "P1_STACK32_DONE=EVIDENCE_CAPTURED no_compiler_codegen_changes=true"

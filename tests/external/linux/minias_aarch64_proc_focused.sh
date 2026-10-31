#!/usr/bin/env bash
# Targeted Linux 6.6.143 AArch64 proc.S differential.  Preserve one Kbuild GCC
# .s, assemble it both ways, and never use GNU objects in the MiniAS result.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$root"
version=${LINUX_VERSION:-6.6.143}
[[ $version == 6.6.143 ]] || { echo 'MINIAS_AARCH64_PROC_ERROR unsupported_version'; exit 2; }
work=${WORK_DIR:-"$root/build/minias-aarch64-proc-focused"}
[[ -n $work && $work != / && $work != "$root" ]] || {
  echo 'MINIAS_AARCH64_PROC_ERROR unsafe_work_directory' >&2; exit 2;
}
mkdir -p "$work"
work=$(cd "$work" && pwd)
src="$work/linux-$version"
out="$work/out"
capture="$work/evidence"
bindir="$work/as-override"
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
for tool in "$cc" aarch64-linux-gnu-as aarch64-linux-gnu-objdump \
  aarch64-linux-gnu-nm aarch64-linux-gnu-objcopy \
  aarch64-linux-gnu-readelf curl sha256sum make python3; do
  command -v "$tool" >/dev/null || {
    printf 'MINIAS_AARCH64_PROC_ERROR missing_tool=%s\n' "$tool" >&2; exit 2;
  }
done
[[ -x $minias ]] || { echo 'MINIAS_AARCH64_PROC_ERROR missing_minias' >&2; exit 2; }
mkdir -p "$capture" "$bindir"

archive="$work/linux-$version.tar.xz"
if [[ ! -s $archive ]]; then
  curl -fL --retry 3 --retry-delay 2 \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" -o "$archive"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
if [[ ! -d $src ]]; then tar -xJf "$archive" -C "$work"; fi

# Precisely mirror minias_aarch64_linux.sh Kconfig; no alternative compiler or
# a hand-crafted approximation of arch/arm64/mm/.proc.o.cmd.
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  defconfig >"$capture/config.log" 2>&1
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_AMBA_PL011 -e SERIAL_AMBA_PL011_CONSOLE \
  -e PROC_FS -e SYSFS -e TMPFS -e IPV6 -e IO_URING
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig >>"$capture/config.log" 2>&1
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 prepare >"$capture/prepare.log" 2>&1
sha256sum "$out/.config" >"$capture/config.sha256"

cat >"$bindir/aarch64-linux-gnu-as" <<'AS_WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
args=("$@")
options=(--target=aarch64)
out=
input=
query=0
for ((i=0; i<${#args[@]}; ++i)); do
  case "${args[i]}" in
    --version|-v|--help) query=1 ;;
    -march=*|-mabi=*) options+=("${args[i]}") ;;
    -o) if ((i+1<${#args[@]})); then out=${args[i+1]}; ((++i)); fi ;;
    -*) ;;
    *) [[ -f ${args[i]} ]] && input=${args[i]} ;;
  esac
done
if ((query)); then exec "$REAL_AS" "$@"; fi
[[ -n $out && -n $input && $input != - ]] || {
  echo 'MINIAS_AARCH64_PROC_ERROR missing_as_io_or_stdin_not_capturable' >&2; exit 2;
}
if [[ $(realpath -m -- "$out") == "$MINIAS_KBUILD_OUT/arch/arm64/mm/proc.o" ]]; then
  cp -- "$input" "$PROC_EVIDENCE/proc.s"
  ref_args=()
  for ((j=0; j<${#args[@]}; ++j)); do
    if [[ ${args[j]} == -o && $((j+1)) -lt ${#args[@]} ]]; then
      ref_args+=(-o "$PROC_EVIDENCE/proc-gnu.o")
      ((++j))
    else
      ref_args+=("${args[j]}")
    fi
  done
  "$REAL_AS" "${ref_args[@]}" >"$PROC_EVIDENCE/gnu-as.log" 2>&1
  printf 'MINIAS_AARCH64_PROC_GNU_REFERENCE=PASS same_gcc_s=1\n'
fi
"$MINIAS" "${options[@]}" -o "$out" "$input"
AS_WRAPPER
chmod +x "$bindir/aarch64-linux-gnu-as"
ln -sfn aarch64-linux-gnu-as "$bindir/as"

export MINIAS="$minias"
export REAL_AS=/usr/bin/aarch64-linux-gnu-as
export PROC_EVIDENCE="$capture"
export MINIAS_KBUILD_OUT="$out"
if ! make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$cc -B$bindir/" AS="$bindir/aarch64-linux-gnu-as" \
  -j4 arch/arm64/mm/proc.o >"$capture/kbuild.log" 2>&1; then
  echo 'MINIAS_AARCH64_PROC_BUILD=FAIL'
  tail -n 80 "$capture/kbuild.log" || true
  exit 1
fi

cp -- "$out/arch/arm64/mm/proc.o" "$capture/proc-minias.o"
cp -- "$out/arch/arm64/mm/.proc.o.cmd" "$capture/proc.o.cmd"
test -s "$capture/proc.s"
test -s "$capture/proc-gnu.o"
test -s "$capture/proc-minias.o"
sha256sum "$capture/proc.s" "$capture/proc-gnu.o" "$capture/proc-minias.o" \
  >"$capture/proc.sha256"
for producer in gnu minias; do
  object="$capture/proc-$producer.o"
  aarch64-linux-gnu-objdump -dr "$object" >"$capture/proc-$producer-objdump.log"
  aarch64-linux-gnu-objdump -dr -j .idmap.text "$object" \
    >"$capture/proc-$producer-idmap-objdump.log"
  aarch64-linux-gnu-nm -n "$object" >"$capture/proc-$producer-nm.log"
  aarch64-linux-gnu-objcopy --dump-section \
    .idmap.text="$capture/proc-$producer-idmap.bin" "$object"
  aarch64-linux-gnu-readelf -SW -Wr -Ws "$object" >"$capture/proc-$producer-elf.log"
done
# The observed Linux P0 culprit is the executable ID-map setup path.  Require
# GNU-identical instruction/data bytes in the runtime sections and the exact
# relocation tuple in .idmap.text.  DWARF-only differences remain diagnostics:
# they cannot substitute for actual Linux QEMU startup acceptance.
python3 - "$capture/proc-gnu.o" "$capture/proc-minias.o" <<'PY'
from elftools.elf.elffile import ELFFile
import sys

def collect(path):
    with open(path, "rb") as handle:
        elf = ELFFile(handle)
        sections = {}
        for name in (".text", ".data", ".altinstructions", ".idmap.text"):
            section = elf.get_section_by_name(name)
            if section is None:
                raise ValueError(f"missing production section {name} in {path}")
            sections[name] = (section.data(), int(section["sh_addralign"]))
        relsec = elf.get_section_by_name(".rela.idmap.text")
        if relsec is None:
            raise ValueError(f"missing ID-map relocations in {path}")
        symtab = elf.get_section(relsec["sh_link"])
        relocations = [
            (int(rel["r_offset"]), int(rel["r_info_type"]),
             symtab.get_symbol(rel["r_info_sym"]).name, int(rel["r_addend"]))
            for rel in relsec.iter_relocations()
        ]
        return sections, relocations

try:
    reference = collect(sys.argv[1])
    candidate = collect(sys.argv[2])
    if reference != candidate:
        for name in reference[0]:
            if reference[0][name] != candidate[0][name]:
                print(f"MINIAS_AARCH64_PROC_RUNTIME=FAIL section={name}", file=sys.stderr)
        if reference[1] != candidate[1]:
            print(f"MINIAS_AARCH64_PROC_RUNTIME=FAIL idmap_relocations={candidate[1]!r}"
                  f" expected={reference[1]!r}", file=sys.stderr)
        raise SystemExit(1)
    if len(reference[0][".idmap.text"][0]) != 936 or len(reference[1]) != 8:
        raise ValueError("unexpected Linux 6.6.143 proc.o ID-map baseline")
except ValueError as error:
    print(f"MINIAS_AARCH64_PROC_RUNTIME=FAIL {error}", file=sys.stderr)
    raise SystemExit(1)
print("MINIAS_AARCH64_PROC_RUNTIME=PASS alloc_sections=4 idmap_bytes=936 idmap_relocs=8 GNU_exact=1")
PY
if python3 tests/assembler/elf_semantic_compare.py \
  "$capture/proc-gnu.o" "$capture/proc-minias.o" \
  --json-out "$capture/proc-semantic.json" --strict \
  >"$capture/proc-semantic.log" 2>&1; then
  echo 'MINIAS_AARCH64_PROC_SEMANTIC=EXACT GNU_vs_MiniAS=1'
else
  echo 'MINIAS_AARCH64_PROC_SEMANTIC=DIFF GNU_vs_MiniAS=1 diagnostics_only=1'
fi
echo 'MINIAS_AARCH64_PROC_BUILD=PASS GCC_source_identical=1 MiniAS_production_only=1'

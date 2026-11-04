#!/usr/bin/env bash
# GNU linker-script selector groups preserve source order; separate selectors
# and SORT_BY_NAME are distinct.  Mergeable and ordinary .rodata fragments
# share one ELF output section only after dropping incompatible M/S/entsize.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ld python3; do
    command -v "$tool" >/dev/null || { echo "MINILD_SCRIPT_MERGE=INCONCLUSIVE missing=$tool" >&2; exit 8; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-merge.XXXXXX")
finish() {
    if [[ ${MINILD_SCRIPT_MERGE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_MERGE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

as -o "$work/gnu.o" tests/linker/script-merge-section-flags.S
as -o "$work/roafter.gnu.o" tests/linker/script-merge-section-roafter.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/rv64.o" \
    tests/linker/script-merge-section-flags-rv64.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/roafter.rv64.o" \
    tests/linker/script-merge-section-roafter.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/homogeneous.o" \
    tests/linker/script-merge-section-homogeneous-rv64.S
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/incompatible.o" \
    tests/linker/script-merge-section-incompatible-rv64.S

for case in grouped separate sorted keep gap roafter; do
    flags=()
    x86_extra=()
    rv64_extra=()
    case "$case" in
        grouped)
            x86=tests/linker/script-merge-section-flags.lds
            rv64=tests/linker/script-merge-section-flags-rv64.lds ;;
        separate)
            x86=tests/linker/script-merge-section-separate.lds
            rv64=tests/linker/script-merge-section-separate-rv64.lds ;;
        sorted)
            x86=tests/linker/script-merge-section-sorted.lds
            rv64=tests/linker/script-merge-section-sorted-rv64.lds ;;
        keep)
            x86=tests/linker/script-merge-section-keep.lds
            rv64=tests/linker/script-merge-section-keep-rv64.lds
            flags=(--gc-sections) ;;
        gap)
            x86=tests/linker/script-merge-section-gap.lds
            rv64=tests/linker/script-merge-section-gap-rv64.lds ;;
        roafter)
            x86=tests/linker/script-merge-section-roafter.lds
            rv64=tests/linker/script-merge-section-roafter-rv64.lds
            x86_extra=("$work/roafter.gnu.o")
            rv64_extra=("$work/roafter.rv64.o") ;;
    esac
    ld -m elf_x86_64 -static "${flags[@]}" -T "$x86" \
        -o "$work/$case.gnu.elf" "$work/gnu.o" "${x86_extra[@]}"
    "$MINILD" -melf64lriscv -static "${flags[@]}" -T "$rv64" \
        -o "$work/$case.rv64.elf" "$work/rv64.o" "${rv64_extra[@]}" \
        >"$work/$case.mini.stdout" 2>"$work/$case.mini.stderr"
    if command -v timeout >/dev/null; then
        observed=$(timeout 5s "$work/$case.gnu.elf")
    else
        observed=$("$work/$case.gnu.elf")
    fi
    if [[ "$observed" != alpha ]]; then
        echo "MINILD_SCRIPT_MERGE=FAIL native_gnu_runtime case=$case" >&2
        exit 1
    fi
    if command -v qemu-riscv64 >/dev/null; then
        qemu-riscv64 "$work/$case.rv64.elf" >/dev/null
    fi
done

cross_gnu=0
if command -v riscv64-linux-gnu-ld >/dev/null; then
    for case in grouped separate sorted keep gap roafter; do
        flags=()
        extra=()
        [[ $case == keep ]] && flags=(--gc-sections)
        case "$case" in
            grouped) script=tests/linker/script-merge-section-flags-rv64.lds ;;
            separate) script=tests/linker/script-merge-section-separate-rv64.lds ;;
            sorted) script=tests/linker/script-merge-section-sorted-rv64.lds ;;
            keep) script=tests/linker/script-merge-section-keep-rv64.lds ;;
            gap) script=tests/linker/script-merge-section-gap-rv64.lds ;;
            roafter)
                script=tests/linker/script-merge-section-roafter-rv64.lds
                extra=("$work/roafter.rv64.o") ;;
        esac
        riscv64-linux-gnu-ld -m elf64lriscv -static "${flags[@]}" \
            -T "$script" -o "$work/$case.rv64-gnu.elf" \
            "$work/rv64.o" "${extra[@]}"
    done
    cross_gnu=1
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo 'MINILD_SCRIPT_MERGE=INCONCLUSIVE missing=riscv64-linux-gnu-ld' >&2
    exit 8
fi

"$MINILD" -melf64lriscv -static \
    -T tests/linker/script-merge-section-homogeneous.lds \
    -o "$work/homogeneous.rv64.elf" "$work/homogeneous.o" \
    >"$work/homogeneous.stdout" 2>"$work/homogeneous.stderr"

for bad in flags type; do
    if "$MINILD" -melf64lriscv -static \
        -T "tests/linker/script-merge-section-bad-$bad.lds" \
        -o "$work/bad-$bad.elf" "$work/incompatible.o" \
        >"$work/bad-$bad.stdout" 2>"$work/bad-$bad.stderr"; then
        echo "MINILD_SCRIPT_MERGE=FAIL accepted_incompatible_$bad" >&2
        exit 1
    fi
    if ! grep -q 'unsupported-script-output-type:\.rodata' \
        "$work/bad-$bad.stderr" || [[ -e "$work/bad-$bad.elf" ]]; then
        echo "MINILD_SCRIPT_MERGE=FAIL wrong_incompatible_$bad_rejection" >&2
        exit 1
    fi
done

python3 - "$work" "$cross_gnu" <<'PY_ELF'
from pathlib import Path
import struct
import sys

work = Path(sys.argv[1])
cross_gnu = sys.argv[2] == "1"
alpha = b"alpha\n\0"
constant = struct.pack("<I", 0x12345678)
normal = struct.pack("<Q", 0x0123456789ABCDEF)
expected = {
    "grouped": alpha + b"\0" + constant + normal,
    "separate": normal + alpha + b"\0" + constant,
    "sorted": constant + alpha + normal,
    "keep": alpha + b"\0" + constant + normal,
    "gap": alpha + b"\0" + constant + normal,
    "roafter": alpha + b"\0" + constant + normal + b"\0" * 4 +
               struct.pack("<Q", 0x1122334455667788),
}


def load_segments(path):
    blob = path.read_bytes()
    phoff = struct.unpack_from("<Q", blob, 32)[0]
    entsize, count = struct.unpack_from("<HH", blob, 54)
    if entsize != 56 or phoff + count * entsize > len(blob):
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL program_headers:{path}")
    return [struct.unpack_from("<IIQQQQQQ", blob, phoff + i * entsize)
            for i in range(count)
            if struct.unpack_from("<I", blob, phoff + i * entsize)[0] == 1]


def verify_no_page_alias(path, expect_single):
    loads = load_segments(path)
    if expect_single and len(loads) != 1:
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL same_page_load_split:{path}:{loads}")
    for i, left in enumerate(loads):
        start = left[3]
        end = start + left[6]
        for right in loads[i + 1:]:
            rstart = right[3]
            rend = rstart + right[6]
            if end > start and rend > rstart and (start // 4096 <=
                    (rend - 1) // 4096 and rstart // 4096 <=
                    (end - 1) // 4096):
                raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL aliased_load_pages:{path}")


def elf_section(path, machine, check_symbols=True):
    blob = path.read_bytes()
    if blob[:6] != b"\x7fELF\x02\x01":
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL ELF_format:{path}")
    if struct.unpack_from("<H", blob, 16)[0] != 2 or (
            struct.unpack_from("<H", blob, 18)[0] != machine):
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL ELF_machine:{path}")
    offset = struct.unpack_from("<Q", blob, 40)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", blob, 58)
    if entsize != 64 or names_index >= count or offset + count * 64 > len(blob):
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL ELF_shdr:{path}")
    headers = [struct.unpack_from("<IIQQQQIIQQ", blob, offset + i * 64)
               for i in range(count)]
    names_hdr = headers[names_index]
    names = blob[names_hdr[4]:names_hdr[4] + names_hdr[5]]

    def name(pos):
        end = names.find(b"\0", pos)
        if end < 0:
            raise SystemExit("MINILD_SCRIPT_MERGE=FAIL shstrtab")
        return names[pos:end].decode("ascii")

    rodata = [(i, sh) for i, sh in enumerate(headers)
              if name(sh[0]) == ".rodata"]
    if len(rodata) != 1:
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL rodata_count:{path}")
    index, sh = rodata[0]
    data = blob[sh[4]:sh[4] + sh[5]]
    if len(data) != sh[5] or sh[1] != 1:
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL rodata_bounds:{path}")
    symtab = [x for x in headers if x[1] == 2]
    if len(symtab) != 1:
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL symtab:{path}")
    stab = symtab[0]
    strs = headers[stab[6]]
    symnames = blob[strs[4]:strs[4] + strs[5]]
    observed = {}
    for pos in range(stab[4], stab[4] + stab[5], 24):
        symname, _, _, shndx, value, _ = struct.unpack_from("<IBBHQQ", blob, pos)
        if symname == 0 or shndx != index:
            continue
        end = symnames.find(b"\0", symname)
        if end < 0:
            raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL strtab:{path}")
        observed[symnames[symname:end].decode("ascii")] = value - sh[3]
    if check_symbols:
        required = [("merge_text", alpha), ("merge_const", constant),
                    ("normal_const", normal)]
        if path.name.startswith("roafter."):
            required.append(("ro_after_init_value",
                             struct.pack("<Q", 0x1122334455667788)))
        for symbol, raw in required:
            where = observed.get(symbol)
            if where is None or data[where:where + len(raw)] != raw:
                raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL symbol:{path}:{symbol}")
    return data, sh[2], sh[9]


for case, wanted in expected.items():
    reference = elf_section(work / f"{case}.gnu.elf", 62)
    candidate = elf_section(work / f"{case}.rv64.elf", 243)
    wanted_flags = 0x3 if case == "roafter" else 0x2
    if reference != candidate or reference != (wanted, wanted_flags, 0):
        raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL GNU_byte_or_flags_diff:{case} "
                         f"reference={reference} candidate={candidate}")
    verify_no_page_alias(work / f"{case}.rv64.elf", expect_single=True)
    if cross_gnu:
        same_isa_reference = elf_section(work / f"{case}.rv64-gnu.elf", 243)
        if same_isa_reference != candidate:
            raise SystemExit(f"MINILD_SCRIPT_MERGE=FAIL GNU_RV64_same_object_diff:{case}")
    print(f"MINILD_SCRIPT_MERGE_CASE=PASS case={case} "
          f"byte_count={len(wanted)} flags=A entsize=0")

homogeneous = elf_section(work / "homogeneous.rv64.elf", 243,
                          check_symbols=False)
if homogeneous[1:] != (0x32, 1):
    raise SystemExit("MINILD_SCRIPT_MERGE=FAIL homogeneous_merge_flags")
print("MINILD_SCRIPT_MERGE=PASS grouped=GNU_bytes separate=GNU_bytes "
      "sorted=GNU_bytes KEEP_GC=GNU_bytes page_gap=GNU_bytes "
      "ro_after_init=GNU_write_union same_page_load_coalesced=1 homogeneous=AMS/1 "
      "incompatible_flags=reject incompatible_type=reject "
      f"same_ISA_GNU={cross_gnu}")
PY_ELF

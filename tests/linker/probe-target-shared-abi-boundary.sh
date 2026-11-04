#!/usr/bin/env bash
# Target decides shared backend and ELF ABI flags. Driver must not guess ISA
# from emulation names. All mutations start from real MiniAS-produced ET_REL.
set -Eeuo pipefail
: "${MINIAS:?MINIAS is required}"
: "${MINILD:?MINILD is required}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-abi.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

for name in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    case "$name" in
        x86_64) target=x86_64 march=x86-64 mabi=sysv ;;
        aarch64) target=aarch64 march=armv8-a mabi=lp64 ;;
        loongarch64) target=loongarch64 march=loongarch64 mabi=lp64d ;;
        riscv64) target=riscv march=rv64i mabi=lp64 ;;
        riscv32) target=riscv march=rv32i mabi=ilp32 ;;
    esac
    case "$name" in
        x86_64) printf '%s\n' '.text' '.globl _start' '_start:' 'ret' >"$work/$name.s" ;;
        aarch64) printf '%s\n' '.text' '.globl _start' '_start:' 'ret' >"$work/$name.s" ;;
        loongarch64) printf '%s\n' '.text' '.globl _start' '_start:' 'jirl $zero,$ra,0' >"$work/$name.s" ;;
        *) printf '%s\n' '.text' '.globl _start' '_start:' 'jalr zero,ra,0' >"$work/$name.s" ;;
    esac
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.o" "$work/$name.s"
done

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
mutations = {
    'x86_64': (0x1,),
    'aarch64': (0x1,),
    'loongarch64': (0, 0x44, 0x81),
}
for name, flags in mutations.items():
    original = (root / f'{name}.o').read_bytes()
    if original[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'MINILD_SHARED_ABI=FAIL not_elf64={name}')
    for flag in flags:
        modified = bytearray(original)
        struct.pack_into('<I', modified, 48, flag)
        (root / f'{name}-{flag:x}-invalid.o').write_bytes(modified)
for flag in (0x41, 0x42):
    original = bytearray((root / 'loongarch64.o').read_bytes())
    struct.pack_into('<I', original, 48, flag)
    (root / f'loongarch64-{flag:x}-valid.o').write_bytes(original)
PY

reject() {
    local id=$1 expected=$2
    shift 2
    if "$MINILD" "$@" -o "$work/$id-should-not-exist.elf" \
        >"$work/$id.stdout" 2>"$work/$id.stderr"; then
        echo "MINILD_SHARED_ABI=FAIL accepted=$id" >&2
        exit 1
    fi
    if [[ -e "$work/$id-should-not-exist.elf" ]] ||
       ! grep -Fq "$expected" "$work/$id.stderr"; then
        echo "MINILD_SHARED_ABI=FAIL not_fail_closed=$id" >&2
        sed -n '1,10p' "$work/$id.stderr" >&2
        exit 1
    fi
}

for name in x86_64 aarch64 loongarch64; do
    case "$name" in
        x86_64) emulation=elf_x86_64 ;;
        aarch64) emulation=aarch64linux ;;
        loongarch64) emulation=elf64loongarch ;;
    esac
    reject "unsupported-shared-$name" unsupported-emulation-or-link-mode \
        -m "$emulation" -shared "$work/$name.o"
    reject "unsupported-pie-$name" unsupported-emulation-or-link-mode \
        -m "$emulation" -pie "$work/$name.o"
done

for item in x86_64:1:elf_x86_64 aarch64:1:aarch64linux \
            loongarch64:0:elf64loongarch \
            loongarch64:44:elf64loongarch \
            loongarch64:81:elf64loongarch; do
    IFS=: read -r name flag emulation <<<"$item"
    for mode in -r -static; do
        reject "badflags-$name-$flag-${mode#-}" unsupported-target-elf-flags \
            -m "$emulation" "$mode" "$work/$name-$flag-invalid.o"
    done
done

# The RV32 shared flag routes to the constrained compat-vDSO owner, which
# rejects a generic DSO contract with a specific target-level diagnostic.
reject rv32-compat-dispatch elf32-unsupported-link-contract \
    -m elf32lriscv -shared -soname=not-compat.so "$work/riscv32.o"
reject rv32-compat-valid-soname-incomplete-contract elf32-unsupported-link-contract \
    -m elf32lriscv -shared -soname=linux-compat_vdso.so.1 "$work/riscv32.o"
reject rv32-pie-contract unsupported-emulation-or-link-mode \
    -m elf32lriscv -pie "$work/riscv32.o"
reject rv32-static-mode unsupported-emulation-or-link-mode \
    -m elf32lriscv -static "$work/riscv32.o"

"$MINILD" -m elf64loongarch -r \
    -o "$work/loongarch64-default-r.o" "$work/loongarch64.o"
for flag in 41 42; do
    "$MINILD" -m elf64loongarch -r \
        -o "$work/loongarch64-$flag-r.o" \
        "$work/loongarch64-$flag-valid.o"
done
python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
for flag in (0x41, 0x42, 0x43):
    suffix = 'default' if flag == 0x43 else f'{flag:x}'
    data = (root / f'loongarch64-{suffix}-r.o').read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<I', data, 48)[0] != flag:
        raise SystemExit(f'MINILD_SHARED_ABI=FAIL lost_loongarch_flags={flag:x}')
PY

"$MINILD" -m elf64lriscv -shared -soname=libminild-test.so \
    -o "$work/riscv64-shared.elf" "$work/riscv64.o"
readelf -h "$work/riscv64-shared.elf" | grep -q 'DYN (Shared object file)' || {
    echo 'MINILD_SHARED_ABI=FAIL rv64_shared_regression' >&2
    exit 1
}
python3 - "$work/riscv64-shared.elf" <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('MINILD_SHARED_ABI=FAIL rv64_not_elf64le')
kind, machine, flags = struct.unpack_from('<HH', data, 16)[0], \
    struct.unpack_from('<H', data, 18)[0], \
    struct.unpack_from('<I', data, 48)[0]
if (kind, machine, flags) != (3, 243, 0):
    raise SystemExit('MINILD_SHARED_ABI=FAIL rv64_wrong_header')
offset = struct.unpack_from('<Q', data, 40)[0]
entry_size, count, names_index = struct.unpack_from('<HHH', data, 58)
if entry_size != 64 or not 0 < names_index < count or \
        offset + count * entry_size > len(data):
    raise SystemExit('MINILD_SHARED_ABI=FAIL rv64_bad_shdr_table')
headers = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * 64)
           for i in range(count)]
name_header = headers[names_index]
strings = data[name_header[4]:name_header[4]+name_header[5]]
sections = {}
for h in headers:
    start = h[0]
    if start >= len(strings):
        raise SystemExit('MINILD_SHARED_ABI=FAIL rv64_invalid_section_name')
    name = strings[start:strings.find(b'\0', start)].decode()
    sections[name] = h
for name, kind, entry_size in (('.dynsym', 11, 24),
                               ('.rela.dyn', 4, 24),
                               ('.dynamic', 6, 16)):
    h = sections.get(name)
    if h is None or h[1] != kind or h[9] != entry_size or \
            h[5] % entry_size:
        raise SystemExit(f'MINILD_SHARED_ABI=FAIL rv64_record_geometry={name}')
if sections['.dynsym'][6] >= count or \
        headers[sections['.dynsym'][6]] != sections['.dynstr']:
    raise SystemExit('MINILD_SHARED_ABI=FAIL rv64_dynsym_link')
print('MINILD_SHARED_ABI_CHECK=PASS target=rv64 '
      'ELFCLASS=64 ET_DYN=true dynsym_entry=24 rela_entry=24 dynamic_entry=16')
PY
echo 'MINILD_SHARED_ABI=PASS rejected_shared_pie=6 invalid_flag_modes=10 rv32_compat_dispatch=3 rv32_static_rejected=PASS loongarch_v1_flags=3 rv64_shared=PASS'

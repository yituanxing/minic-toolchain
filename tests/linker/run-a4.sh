#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/linker/a4"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/shared.s" <<'EOF'
.text
.globl exported_answer
.type exported_answer, @function
exported_answer:
  li a0, 42
  ret
.size exported_answer, .-exported_answer

.data
.align 3
.local local_value
.type local_value, @object
local_value:
  .dword 7
.size local_value, 8

.globl exported_ptr
.type exported_ptr, @object
exported_ptr:
  .dword local_value
.size exported_ptr, 8

.globl external_ptr
.type external_ptr, @object
external_ptr:
  .dword external_value
.size external_ptr, 8
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/shared.o" "$work/shared.s"

"$LD" -melf64lriscv -shared -soname libminild-a4.so \
  -o "$work/reference.so" "$work/shared.o"

"$MINILD" -melf64lriscv -shared -soname libminild-a4.so \
  -o "$work/product.so" "$work/shared.o"

"$READELF" -h "$work/product.so" >"$work/product.header"
"$READELF" -l "$work/product.so" >"$work/product.programs"
"$READELF" -S "$work/product.so" >"$work/product.sections"
"$READELF" -d "$work/product.so" >"$work/product.dynamic"
"$READELF" -Ws "$work/product.so" >"$work/product.symbols"
"$READELF" -Wr "$work/product.so" >"$work/product.relocs"

grep -q 'DYN (Shared object file)' "$work/product.header"
grep -q 'RISC-V' "$work/product.header"
grep -q ' DYNAMIC ' "$work/product.programs"
grep -Eq '] \.dynstr[[:space:]]+STRTAB' "$work/product.sections"
grep -Eq '] \.dynsym[[:space:]]+DYNSYM' "$work/product.sections"
grep -Eq '] \.hash[[:space:]]+HASH' "$work/product.sections"
grep -Eq '] \.rela\.dyn[[:space:]]+RELA' "$work/product.sections"
grep -Eq '] \.dynamic[[:space:]]+DYNAMIC' "$work/product.sections"
grep -q '(SONAME).*\[libminild-a4.so\]' "$work/product.dynamic"
grep -q '(HASH)' "$work/product.dynamic"
grep -q '(STRTAB)' "$work/product.dynamic"
grep -q '(SYMTAB)' "$work/product.dynamic"
grep -q '(RELA)' "$work/product.dynamic"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* exported_answer$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* exported_ptr$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* external_value$' "$work/product.symbols"
grep -q 'R_RISCV_RELATIVE' "$work/product.relocs"
grep -Eq 'R_RISCV_64.*external_value' "$work/product.relocs"

reference_relative="$(grep -c 'R_RISCV_RELATIVE' "$work/reference.so" 2>/dev/null || true)"
product_relative="$(grep -c 'R_RISCV_RELATIVE' "$work/product.relocs")"
test "$product_relative" -ge 1

# Linux vDSO needs both hash table formats in its shared ELF.
"$MINILD" -melf64lriscv -shared --hash-style=both \
  -soname libminild-a4.so -o "$work/product-hash-both.so" "$work/shared.o"
"$READELF" -SW "$work/product-hash-both.so" >"$work/hash-both.sections"
"$READELF" -dW "$work/product-hash-both.so" >"$work/hash-both.dynamic"
grep -Eq '] \.gnu\.hash[[:space:]]+GNU_HASH' "$work/hash-both.sections"
grep -Eq '] \.hash[[:space:]]+HASH' "$work/hash-both.sections"
grep -q '(GNU_HASH)' "$work/hash-both.dynamic"
grep -q '(HASH)' "$work/hash-both.dynamic"
echo "MINILD_A4_GNU_HASH=PASS both=true"

# Linux RISC-V vDSO uses zR/zRS CIE, pcrel|sdata4 FDEs.
# A real FDE must be indexed and advertised via PT_GNU_EH_FRAME.
cat >"$work/eh-frame.s" <<'EOF_EH'
.text
.globl unwind_probe
.type unwind_probe,@function
unwind_probe:
  .cfi_startproc
  addi a0,a0,1
  ret
  .cfi_endproc
.size unwind_probe,.-unwind_probe
EOF_EH
"$AS" -march=rv64gc -mabi=lp64d -o "$work/eh-frame.o" "$work/eh-frame.s"
"$MINILD" -melf64lriscv -shared --eh-frame-hdr -o "$work/product-eh-frame.so" "$work/eh-frame.o"
"$READELF" -W -l "$work/product-eh-frame.so" >"$work/eh-frame.programs"
"$READELF" -W -S "$work/product-eh-frame.so" >"$work/eh-frame.sections"
grep -q 'GNU_EH_FRAME' "$work/eh-frame.programs"
grep -Eq '] \.eh_frame_hdr[[:space:]]+PROGBITS' "$work/eh-frame.sections"
python3 - "$work/product-eh-frame.so" <<'PY_EH'
import pathlib,struct,sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
shoff=struct.unpack_from("<Q",raw,40)[0]
shentsize,shnum,shstrndx=struct.unpack_from("<HHH",raw,58)
base=shoff+shstrndx*shentsize
str_off,str_len=struct.unpack_from("<QQ",raw,base+24)
names=raw[str_off:str_off+str_len]
found=False
for i in range(shnum):
    b=shoff+i*shentsize
    no=struct.unpack_from("<I",raw,b)[0]
    name=names[no:names.find(b"\0",no)].decode()
    if name==".eh_frame_hdr":
        off,size=struct.unpack_from("<QQ",raw,b+24)
        hdr=raw[off:off+size]
        assert hdr[:4]==bytes.fromhex("011b033b"),hdr[:4].hex()
        assert struct.unpack_from("<I",hdr,8)[0]==1
        assert len(hdr)==20
        found=True
assert found
print("MINILD_A4_EH_FRAME_HDR=PASS fdes=1 header=20 PT_GNU_EH_FRAME=true")
PY_EH

# Linux RISC-V vDSO uses the GNU spelling -S. On debug-free
# ET_REL input, stripping debug info is an actual no-op and may succeed.
# Test that -S does not accidentally turn into a positional input.
"$MINILD" -melf64lriscv -shared -S -soname libminild-a4.so \
  -o "$work/product-strip-no-debug.so" "$work/shared.o"
"$READELF" -h "$work/product-strip-no-debug.so" > "$work/strip-no-debug.header"
grep -q 'DYN (Shared object file)' "$work/strip-no-debug.header"

# Until genuine debug section elision exists, -S must fail closed instead
# of publishing an ELF that claims stripped debug while retaining sections.
cat > "$work/has-debug.s" <<'EOF_DEBUG'
.text
.globl debug_noop
debug_noop:
  ret
.section .debug_info,"",@progbits
.byte 0x11, 0x22, 0x33
EOF_DEBUG
"$AS" -march=rv64gc -mabi=lp64d -o "$work/has-debug.o" "$work/has-debug.s"
if "$MINILD" -melf64lriscv -shared -S -soname libminild-a4.so \
     -o "$work/strip-debug-unsupported.so" "$work/has-debug.o" \
     > "$work/strip-debug.stdout" 2>"$work/strip-debug.stderr"; then
  echo "MINILD_A4_STRIP=FAIL silently_accepted_unstripped_debug" >&2
  exit 1
fi
grep -Fq "unsupported-shared-strip-debug-section:.debug_info" \
  "$work/strip-debug.stderr"
test ! -e "$work/strip-debug-unsupported.so"
echo "MINILD_A4_STRIP=PASS empty_debug_noop_and_nonempty_debug_fail_closed"

# A shared -T must NOT be silently ignored. Until real shared linker-script
# placement/symbol expressions exist, report a deterministic capability gap
# and leave no misleading output ELF. The Linux vDSO depends on _vdso_data.
cat >"$work/shared.lds" <<'EOF_SCRIPT'
SECTIONS {
  .text : { *(.text*) }
  _vdso_data = .;
}
EOF_SCRIPT
if "$MINILD" -melf64lriscv -shared -soname libminild-a4.so \
     -T "$work/shared.lds" -o "$work/script-unsupported.so" \
     "$work/shared.o" >"$work/shared-script.stdout" \
     2>"$work/shared-script.stderr"; then
  echo "MINILD_A4=FAIL shared_T_was_silently_ignored" >&2
  exit 1
fi
grep -Fq "unsupported-shared-linker-script:$work/shared.lds" \
  "$work/shared-script.stderr"
test ! -e "$work/script-unsupported.so"
echo "MINILD_A4_SCRIPT_GUARD=PASS unsupported_shared_T_rejected_without_ELF=true"

echo "MINILD_A4=PASS et_dyn=PASS dynsym=PASS dynamic=PASS relative=PASS external64=PASS soname=PASS"

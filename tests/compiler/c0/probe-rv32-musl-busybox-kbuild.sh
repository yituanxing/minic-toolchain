#!/usr/bin/env bash
# DISCOVERY, not a BusyBox acceptance certificate.
# Genuine native Linux musl 1.2.5 / RV32 ILP32 sysroot and pristine
# BusyBox 1.36.1 Kconfig/Kbuild, no GCC C fallback or simulated syscall libc.
# Only graduate after original GNU reference links AND MiniC all-owners
# replace through the existing BusyBox Kbuild/sha/QEMU oracle.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC}"
: "${BARE_CC:=riscv64-unknown-elf-gcc}"
: "${GNU_LD:=riscv64-linux-gnu-ld}"
: "${QEMU_RV32:=qemu-riscv32}"
repo=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for tool in "$BARE_CC" "$GNU_LD" "$QEMU_RV32" curl tar sha256sum make git; do
  command -v "$tool" >/dev/null
done
# Note: this is NOT glibc, and is NOT the older Picolibc Linux shim. It
# uses musl's genuine riscv32 Linux syscall and crt code from upstream.
# musl's lock/atomic runtime may legitimately use A while MiniC's C
# producer remains strict RV32I. Both share ILP32 soft-float ABI.
url=https://musl.libc.org/releases/musl-1.2.5.tar.gz
hash=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
curl -fsSL --retry 3 --max-time 120 "$url" -o "$work/musl.tgz"
printf '%s  %s\n' "$hash" "$work/musl.tgz" | sha256sum -c --status
tar -xzf "$work/musl.tgz" -C "$work"
test -f "$work/musl-1.2.5/arch/riscv32/syscall_arch.h"
echo "RV32_MUSL_SOURCE=VERIFIED version=1.2.5 sha256=$hash target=riscv32-linux-musl"
sysroot="$work/sysroot"
mkdir -p "$work/build" "$sysroot/usr/include"
# Genuine Linux kernel UAPI headers only; do NOT mix host LP64 glibc's
# bits/, sys/, stdio.h or other runtime headers into RV32 musl.
uapi=/usr/riscv64-linux-gnu/include
for name in linux asm asm-generic mtd; do
  if [ -d "$uapi/$name" ]; then
    cp -a "$uapi/$name" "$sysroot/usr/include/"
  fi
done
if [ ! -f "$sysroot/usr/include/linux/types.h" ]; then
  echo "RV32_MUSL=BLOCKED stage=target_Linux_UAPI missing=$uapi/linux/types.h" >&2
  exit 1
fi
(
  cd "$work/build"
  CC="$BARE_CC" AR=riscv64-unknown-elf-ar RANLIB=riscv64-unknown-elf-ranlib \
    CFLAGS="-O2 -march=rv32ia -mabi=ilp32 -fno-pic -fno-pie" \
    "$work/musl-1.2.5/configure" \
      --target=riscv32-linux-musl --disable-shared --prefix=/usr \
    >"$work/musl-configure.log" 2>&1 || {
      echo "RV32_MUSL=BLOCKED stage=configure" >&2
      tail -n 35 "$work/musl-configure.log" >&2
      exit 1
    }
  make -j4 lib/libc.a obj/crt/crt1.o obj/crt/crti.o obj/crt/crtn.o \
    >"$work/musl-build.log" 2>&1 || {
      echo "RV32_MUSL=BLOCKED stage=build" >&2
      tail -n 35 "$work/musl-build.log" >&2
      exit 1
    }
  make DESTDIR="$sysroot" install-headers >"$work/musl-headers.log" 2>&1
)
test -s "$work/build/lib/libc.a"
test -s "$sysroot/usr/include/stdio.h"
target=(-march=rv32i -mabi=ilp32)
builtins=$("$BARE_CC" "${target[@]}" -print-file-name=include)
libgcc=$("$BARE_CC" "${target[@]}" -print-libgcc-file-name)
test -f "$libgcc"
headers=(-nostdinc -isystem "$sysroot/usr/include" -isystem "$builtins")
cat >"$work/abi.c" <<'SRC'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *text = "native-rv32-musl-1.2.5";
    if (sizeof(void*) != 4 || sizeof(long) != 4 ||
        strlen(text) != 22U) return 30;
    puts(text);
    return 17;
}
SRC
"$BARE_CC" "${target[@]}" "${headers[@]}" -O0 -ffreestanding \
    -fno-builtin -E -P "$work/abi.c" -o "$work/abi.i"
"$BARE_CC" "${target[@]}" "${headers[@]}" -O0 -ffreestanding \
    -fno-builtin -c "$work/abi.c" -o "$work/gcc.o"
"$MINIC_CC" --target=riscv32-linux "${target[@]}" -S \
    "$work/abi.i" -o "$work/mini.s"
"$BARE_CC" "${target[@]}" -c "$work/mini.s" -o "$work/mini.o"
for mode in gcc mini; do
  "$GNU_LD" -m elf32lriscv -static -e _start \
      "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" \
      "$work/$mode.o" --start-group "$work/build/lib/libc.a" \
      "$libgcc" --end-group "$work/build/obj/crt/crtn.o" \
      -o "$work/$mode.elf" \
      >"$work/$mode.link.log" 2>&1 || {
        echo "RV32_MUSL=BLOCKED stage=GNU_LD mode=$mode" >&2
        head -n 45 "$work/$mode.link.log" >&2
        exit 1
      }
  set +e
  timeout 45s "$QEMU_RV32" "$work/$mode.elf" \
      >"$work/$mode.out" 2>"$work/$mode.err"
  rc=$?
  set -e
  if [ "$rc" -ne 17 ]; then
    echo "RV32_MUSL=BLOCKED stage=QEMU mode=$mode rc=$rc" >&2
    head -n 25 "$work/$mode.err" >&2
    exit 1
  fi
done
cmp "$work/gcc.out" "$work/mini.out"
cmp "$work/gcc.err" "$work/mini.err"
echo "RV32_MUSL_ABI=PASS upstream=1.2.5 libc=original_syscalls CRT=musl ELF32=1 ABI=ilp32 GNU_MiniC_QEMU_byte_exact=1"

# The first BusyBox milestone is a real, immutable upstream Kbuild
# reference build. It must succeed with genuine musl before any claim
# about MiniC's percentage or an executable. Minimal applets are an
# explicit configuration frontier, NOT all-applets BusyBox acceptance.
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
curl -fsSL --retry 3 --max-time 120 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/busybox.tgz"
mkdir -p "$work/busybox" "$work/busybox-out"
tar -xzf "$work/busybox.tgz" -C "$work/busybox" --strip-components=1
sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" \
  "$work/busybox"
make -C "$work/busybox" O="$work/busybox-out" \
  HOSTCC=cc allnoconfig >"$work/kconfig.log" 2>&1 || {
    echo "RV32_BUSYBOX=BLOCKED stage=original_Kconfig" >&2
    tail -n 35 "$work/kconfig.log" >&2
    exit 1
  }
# BusyBox's Kconfig is source-owned. Enable a small coherent original
# utility cohort; do not modify any of the vendor's C source files.
python3 - "$work/busybox-out/.config" <<'PY'
import sys
p=sys.argv[1]
# musl on 32-bit Linux has genuine 64-bit off_t. BusyBox's own
# libbb.h explicitly requires CONFIG_LFS=y on that ABI.
names={"STATIC","LFS","ECHO","TRUE","FALSE","CAT","HEAD","WC"}
lines=open(p).readlines()
for i,line in enumerate(lines):
    for name in names:
        if line.strip()==f"# CONFIG_{name} is not set":
            lines[i]=f"CONFIG_{name}=y\n"
        elif line.startswith(f"CONFIG_{name}="):
            lines[i]=f"CONFIG_{name}=y\n"
open(p,"w").writelines(lines)
PY
# BusyBox 1.36.1 Kconfig has 'oldconfig', not Linux's newer
# 'olddefconfig' target. Answer optional defaults without modifying C.
{ yes "" || :; } | timeout 90s make -C "$work/busybox" O="$work/busybox-out" \
  HOSTCC=cc oldconfig >"$work/olddefconfig.log" 2>&1 || {
    echo "RV32_BUSYBOX=BLOCKED stage=olddefconfig" >&2
    tail -n 32 "$work/olddefconfig.log" >&2
    exit 1
  }
# GNU reference Kbuild with the exact musl headers, never Picolibc or
# RV64 host glibc. The command-line driver is stable for all original
# Kbuild C owners. Link closure is explicitly verified.
# Kbuild controls the original C owners. The driver wrapper does not
# replace any source or C object: it supplies the genuine native musl CRT
# BEFORE Kbuild owners and libc/libgcc AFTER them on the link command.
# BusyBox trylink passes EXTRA_LDFLAGS ahead of its entire object group,
# so archives there become duplicate-owned startup/library objects.
# libm is part of real upstream musl libc.a; bare GNU -lm is not installed.
cat >"$work/rv32-linux-musl-gcc" <<EOF
#!/usr/bin/env bash
set -Eeuo pipefail
args=( "\$@" )
base=( "$BARE_CC" -march=rv32i -mabi=ilp32
  -nostdinc -isystem "$sysroot/usr/include" -isystem "$builtins"
  -I"$repo/tests/linker/fixtures/busybox-uapi" -fno-builtin )
compile=0
relocatable=0
for item in "\${args[@]}"; do
  case "\$item" in
    -c|-E|-S|-M|-MM) compile=1 ;;
    -r|-Wl,-r|-Wl,--relocatable) relocatable=1 ;;
  esac
done
if ((compile)); then
  exec "\${base[@]}" "\${args[@]}"
fi
if ((relocatable)); then
  # BusyBox Kbuild combines objects with ld -r. Injecting a CRT into
  # this partial link would copy _start and libc into built-in.o.
  exec "\${base[@]}" -nostdlib "\${args[@]}"
fi
link_args=()
for item in "\${args[@]}"; do
  case "\$item" in -lm) ;; *) link_args+=( "\$item" ) ;; esac
done
exec "\${base[@]}" -nostdlib -static -Wl,-e,_start \
  "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" \
  "\${link_args[@]}" -Wl,--start-group "$work/build/lib/libc.a" \
  "$libgcc" -Wl,--end-group "$work/build/obj/crt/crtn.o"
EOF
chmod +x "$work/rv32-linux-musl-gcc"
# The original BusyBox linker defaults assume GCC drives a native GNU
# libc. For this initial build probe, use its own Kbuild owner inventory
# and fail explicitly if the GNU/musl baseline cannot link.
set +e
timeout 240s make -C "$work/busybox" O="$work/busybox-out" -j4 \
    ARCH=riscv CC="$work/rv32-linux-musl-gcc" HOSTCC=cc \
    STRIP=riscv64-linux-gnu-strip \
    busybox >"$work/busybox-build.log" 2>&1
rc=$?
set -e
owners=$(find "$work/busybox-out" -type f -name '*.o' \
    ! -path '*/scripts/*' | wc -l)
echo "RV32_BUSYBOX_KBUILD_DISCOVERY pinned=1.36.1 owners=$owners rc=$rc"
if [ "$rc" -ne 0 ]; then
    echo "RV32_BUSYBOX=BLOCKED stage=GNU_original_Kbuild rc=$rc" >&2
    grep -E 'error:|fatal error:|No rule|undefined reference|Error [0-9]+' \
      "$work/busybox-build.log" | head -n 28 >&2 || true
    tail -n 30 "$work/busybox-build.log" >&2
    exit 1
fi
test -s "$work/busybox-out/busybox"
binary="$work/busybox-out/busybox"
riscv64-linux-gnu-readelf -h "$binary" | grep -q 'Class:.*ELF32'
riscv64-linux-gnu-readelf -h "$binary" | grep -q 'Machine:.*RISC-V'
timeout 20s "$QEMU_RV32" "$binary" echo native-rv32-busybox \
  >"$work/busybox-echo.out" 2>"$work/busybox-echo.err"
printf 'native-rv32-busybox\n' >"$work/echo.expected"
cmp "$work/echo.expected" "$work/busybox-echo.out"
timeout 20s "$QEMU_RV32" "$binary" true
set +e
timeout 20s "$QEMU_RV32" "$binary" false
false_rc=$?
set -e
test "$false_rc" -eq 1
printf 'first\nsecond\nthird\n' >"$work/stdin.expected"
timeout 20s "$QEMU_RV32" "$binary" cat \
  <"$work/stdin.expected" >"$work/cat.out"
cmp "$work/stdin.expected" "$work/cat.out"
timeout 20s "$QEMU_RV32" "$binary" head -n 2 \
  <"$work/stdin.expected" >"$work/head.out"
printf 'first\nsecond\n' >"$work/head.expected"
cmp "$work/head.expected" "$work/head.out"
wc_count=$(timeout 20s "$QEMU_RV32" "$binary" wc -l \
  <"$work/stdin.expected")
test "$(printf '%s' "$wc_count" | tr -d '[:space:]')" = 3
echo "RV32_BUSYBOX_GNU_KBUILD=PASS original=1.36.1 owners=$owners libc=musl1.2.5 ELF32=1"
echo "RV32_BUSYBOX_GNU_QEMU=PASS original=1.36.1 applets=echo,true,false,cat,head,wc cases=6"
# Side-channel original-Kbuild GNU C owner scan. Reuse the exact existing
# BusyBox compiler proxy rather than writing a second BusyBox/Kbuild parser.
# The reference executable above is immutable. This shadow build is GNU-
# owned and records every actual GNU CPP -> MiniC -> GNU AS frontier;
# no MiniC objects are inserted yet, so success here is never mislabelled
# a fully MiniC executable.
shadow="$work/busybox-shadow"
report="$work/busybox-shadow-report"
mkdir -p "$shadow" "$report/per-tu"
cp "$work/busybox-out/.config" "$shadow/.config"
{ yes "" || :; } | timeout 90s make -C "$work/busybox" O="$shadow" \
  HOSTCC=cc oldconfig >"$work/shadow-oldconfig.log" 2>&1 || {
    echo "RV32_BUSYBOX_MINIC_CENSUS=BLOCKED stage=shadow_Kconfig" >&2
    tail -n 25 "$work/shadow-oldconfig.log" >&2
    exit 1
  }
set +e
timeout 240s env \
  BUSYBOX_ALLYES_HOST_CC="$work/rv32-linux-musl-gcc" \
  BUSYBOX_ALLYES_MINIC_CC="$MINIC_CC" \
  BUSYBOX_ALLYES_MINIC_TARGET=riscv32-linux \
  BUSYBOX_ALLYES_MINIC_PIC=0 \
  BUSYBOX_ALLYES_REPORT_ROOT="$report" \
  make -C "$work/busybox" O="$shadow" -j4 \
    ARCH=riscv HOSTCC=cc \
    CC="python3 $repo/tests/external/busybox/allyes_sweep.py --proxy-cc" \
    STRIP=riscv64-linux-gnu-strip busybox \
  >"$work/shadow-build.log" 2>&1
shadow_rc=$?
set -e
python3 - "$report" "$shadow" "$shadow_rc" <<'PY'
import collections, json, pathlib, sys
report, out, rc = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), int(sys.argv[3])
results=[json.loads(p.read_text()) for p in sorted((report/"per-tu").glob("*/result.json"))]
tally=collections.Counter()
failed=[]
for x in results:
    g=x.get("gcc")
    cpp=x.get("cpp")
    mini=x.get("minic")
    asm=x.get("assembler")
    if g != 0:
        tally["GNU"]+=1
    elif cpp != 0:
        tally["CPP"]+=1
    elif mini != 0:
        tally["MiniC"]+=1
    elif asm != 0:
        tally["GNU_AS"]+=1
    else:
        tally["PASS"]+=1
    if g or cpp or mini or asm:
        failed.append((x["source"],"GCC=%s CPP=%s MiniC=%s GNU_AS=%s"%(g,cpp,mini,asm)))
print("RV32_BUSYBOX_MINIC_CENSUS=DISCOVERY original_Kbuild=1 "
      "GNU_owned=%d MiniC_GNU_AS_pass=%d GNU_blocked=%d CPP_blocked=%d "
      "MiniC_blocked=%d GNU_AS_blocked=%d pinned_source_verified=1 "
      "GNU_shadow_Kbuild_rc=%d MiniC_objects_substituted=0"%
      (len(results),tally["PASS"],tally["GNU"],tally["CPP"],
       tally["MiniC"],tally["GNU_AS"],rc),flush=True)
for name,verdict in failed[:30]:
    print("RV32_BUSYBOX_MINIC_BLOCKER source=%s %s"%(name,verdict),flush=True)
# Evidence must be actionable. Group exact MiniC diagnostics, instead
# of requiring one new CI run per original Kbuild translation unit.
for p in sorted((report/"per-tu").glob("*/result.json")):
    x=json.loads(p.read_text())
    if x.get("minic") != 0 and x.get("gcc")==0 and x.get("cpp")==0:
        lines=(p.parent/"minic.err").read_text(errors="replace").splitlines()
        print("RV32_BUSYBOX_MINIC_OWNER_DIAGNOSTIC source=%s"%x["source"],flush=True)
        for line in lines[:26]:
            print("  %s"%line[:400],flush=True)
if rc or len(results)<50:
    sys.exit(1)
PY

# Bounded real mixed-owner A/B: replace only genuine C owners which passed
# MiniC AND GNU AS in this exact, immutable Kbuild configuration. Leave
# remaining genuine GCC owners untouched. This is diagnostic until 100%
# replacement + SHA receipts + differential QEMU pass. Reuse the existing
# BusyBox proxy's exact-object replacement map, not a new Kbuild parser.
python3 - "$report" "$shadow" <<'PY'
import json, pathlib, sys
report, out=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2])
mapping={}
for p in sorted((report/"per-tu").glob("*/result.json")):
    d=json.loads(p.read_text())
    if all(d.get(k)==0 for k in ("gcc","cpp","minic","assembler")):
        src_obj=pathlib.Path(d["object"])
        absolute=(src_obj if src_obj.is_absolute() else out/src_obj).resolve()
        candidate=(p.parent/"mini.o").resolve()
        if absolute in mapping or not candidate.is_file():
            raise SystemExit("duplicate or missing MiniC owner: "+str(absolute))
        mapping[str(absolute)]=str(candidate)
        if absolute.name.startswith("tmp.") and absolute.name.endswith(".o"):
            print("RV32_BUSYBOX_TEMP_OWNER source=%s object=%s"%
                  (d["source"],absolute),flush=True)
(report/"replacement.json").write_text(json.dumps(mapping,sort_keys=True))
print("RV32_BUSYBOX_MIXED_PLAN=DISCOVERY MiniC_ready=%d GNU_fallback_count=unknown"%
      len(mapping),flush=True)
PY
# Force native Kbuild to regenerate every C object, then reuse the
# established post-GNU-compile SHA-exact MiniC substitution path.
# Never hand-build a bespoke object graph or alter vendor C/Kconfig.
find "$shadow" -type f \( -name '*.o' -o -name '*.a' \) -delete
set +e
timeout 240s env \
  BUSYBOX_ALLYES_HOST_CC="$work/rv32-linux-musl-gcc" \
  BUSYBOX_ALLYES_MINIC_CC="$MINIC_CC" \
  BUSYBOX_ALLYES_MINIC_TARGET=riscv32-linux \
  BUSYBOX_ALLYES_REPORT_ROOT="$report" \
  BUSYBOX_ALLYES_REPLACE_MAP="$report/replacement.json" \
  BUSYBOX_ALLYES_SKIP_SHADOW=1 \
  make -C "$work/busybox" O="$shadow" -j4 \
    ARCH=riscv HOSTCC=cc \
    CC="python3 $repo/tests/external/busybox/allyes_sweep.py --proxy-cc" \
    STRIP=riscv64-linux-gnu-strip busybox \
  >"$work/mixed-build.log" 2>&1
mixed_rc=$?
set -e
python3 - "$report/replacement.json" <<'PY'
import hashlib,json,pathlib,sys
mapping=json.loads(pathlib.Path(sys.argv[1]).read_text())
verified=0
for dest,mini in mapping.items():
    a,b=pathlib.Path(dest),pathlib.Path(mini)
    actual=hashlib.sha256(a.read_bytes()).hexdigest() if a.is_file() else "MISSING"
    expected=hashlib.sha256(b.read_bytes()).hexdigest() if b.is_file() else "MISSING"
    if actual==expected and actual!="MISSING":
        verified+=1
    else:
        print("RV32_BUSYBOX_MIXED_SHA_MISMATCH owner=%s expected=%s actual=%s"%(
              dest,expected,actual),flush=True)
print("RV32_BUSYBOX_MIXED_SHA=DISCOVERY planned=%d verified=%d"%(
      len(mapping),verified),flush=True)
if verified!=len(mapping):
    sys.exit(1)
PY
if [ "$mixed_rc" -ne 0 ] || [ ! -s "$shadow/busybox" ]; then
  echo "RV32_BUSYBOX_MIXED=BLOCKED stage=GNU_link_after_real_MiniC_owner_replacement rc=$mixed_rc" >&2
  grep -E 'error:|undefined reference|multiple definition|Error [0-9]+' \
    "$work/mixed-build.log" | head -n 30 >&2 || true
  exit 1
fi
timeout 20s "$QEMU_RV32" "$shadow/busybox" echo native-rv32-busybox \
  >"$work/mixed.echo.out"
cmp "$work/echo.expected" "$work/mixed.echo.out"
timeout 20s "$QEMU_RV32" "$shadow/busybox" true
set +e
timeout 20s "$QEMU_RV32" "$shadow/busybox" false
mixed_false_rc=$?
set -e
test "$mixed_false_rc" -eq 1
timeout 20s "$QEMU_RV32" "$shadow/busybox" cat \
  <"$work/stdin.expected" >"$work/mixed.cat.out"
cmp "$work/stdin.expected" "$work/mixed.cat.out"
timeout 20s "$QEMU_RV32" "$shadow/busybox" head -n 2 \
  <"$work/stdin.expected" >"$work/mixed.head.out"
cmp "$work/head.expected" "$work/mixed.head.out"
mixed_wc_count=$(timeout 20s "$QEMU_RV32" "$shadow/busybox" wc -l \
  <"$work/stdin.expected")
test "$(printf '%s' "$mixed_wc_count" | tr -d '[:space:]')" = 3
echo "RV32_BUSYBOX_MIXED=DISCOVERY real_MiniC_owners=SHA_verified QEMU_applets=6 GNU_C_fallback=YES full_MiniC=NO"
echo "RV32_BUSYBOX_MINIC_ACCEPT=PENDING full_Kbuild_MiniC_C_owners=NO GNU_C_fallback_disallowed=1"

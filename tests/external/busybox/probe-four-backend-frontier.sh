#!/bin/sh
# BusyBox 1.36.1: ORIGINAL upstream Kconfig/Kbuild and nine applet C owners.
# x86-64 nine-applet object-swap oracle + RV64 original Kbuild wrapper informed
# this design. GNU owns CPP/AS/link/CRT/libc; MiniC owns ONLY selected applets.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
mkdir -p "$work/upstream"
timeout 120s curl -fsSL --retry 2 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/src.tar.gz"
tar -xzf "$work/src.tar.gz" -C "$work/upstream" --strip-components=1
root="$work/upstream"
# Reuse exact pinned source identities from the accepted x86 fixture.
for item in \
  Makefile:fa8cb761443388730372eaf260e7ca1a0235bc43 \
  coreutils/cat.c:4b3414941e87a47bd639bc481ab9f3fa0045ea80 \
  coreutils/echo.c:44b2eb5d05fac12374b5f8692990014087cfda01 \
  coreutils/head.c:c7537a20e4e01483f3a6ffaf89eb2da25d7acf5f \
  coreutils/wc.c:d5238d3fcfba2f62f6bb292a3af55c5107c3ec93 \
  findutils/grep.c:0b72812f18b9177855e226c78f3625146b8daee2 \
  coreutils/cut.c:55bdd938611feb51e7bf1b7977f87015b2819cf9 \
  coreutils/sort.c:01b7c44e5e2cc72027269848b9c025abe45c7506 \
  coreutils/uniq.c:06c57f750be6a94de5189495688a3150385166aa \
  coreutils/tr.c:1e402dfdbd4f047677b8f72ac884eb6e38110fb5; do
  rel=${item%%:*}; expected=${item#*:}
  test "$(git hash-object "$root/$rel")" = "$expected"
done
echo "FOUR_BACKEND_BUSYBOX_SOURCE=PASS version=1.36.1 commit=$commit"
sources='coreutils/cat.c coreutils/echo.c coreutils/head.c coreutils/wc.c findutils/grep.c coreutils/cut.c coreutils/sort.c coreutils/uniq.c coreutils/tr.c'
next_targets=0
wide_targets=0
for target in aarch64 loongarch64; do
  case "$target" in
    aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64
             prefix=aarch64-linux-gnu-; sysroot=/usr/aarch64-linux-gnu ;;
    loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64
                 prefix=loongarch64-linux-gnu-; sysroot=/usr/loongarch64-linux-gnu ;;
  esac
  src="$work/$target/src"
  mkdir -p "$src" "$work/$target/gnu" "$work/$target/mini"
  (cd "$root" && tar -cf - .) | (cd "$src" && tar -xf -)
  echo "FOUR_BACKEND_BUSYBOX_START target=$target"
  if ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
      CROSS_COMPILE="$prefix" allnoconfig \
      >"$work/$target/config.out" 2>"$work/$target/config.err"); then
    echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=GNU_Kconfig"
    tail -n 12 "$work/$target/config.err"; continue
  fi
  python3 - "$src/.config" <<'PY'
import sys
p=sys.argv[1]
s=open(p,encoding="utf8").read()
for k in "CAT ECHO HEAD WC GREP CUT SORT UNIQ TR".split():
    old="# CONFIG_"+k+" is not set"
    new="CONFIG_"+k+"=y"
    if old in s: s=s.replace(old,new)
    elif new not in s: raise SystemExit("BUSYBOX_CONFIG_MISSING="+k)
open(p,"w",encoding="utf8").write(s)
PY
  if ! (cd "$src" && yes '' | make HOSTCC=cc CC="$gcc" \
      CROSS_COMPILE="$prefix" oldconfig \
      >"$work/$target/oldconfig.out" 2>"$work/$target/oldconfig.err") ||
     ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
      CROSS_COMPILE="$prefix" busybox \
      >"$work/$target/build.out" 2>"$work/$target/build.err"); then
    echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=GNU_original_build"
    tail -n 25 "$work/$target/build.err" || true; continue
  fi
  cp "$src/busybox" "$work/$target/gnu/busybox"
  passed=0; blocked=0
  for file in $sources; do
    stem=$(basename "$file" .c)
    inp="$work/$target/$stem.i"
    asm="$work/$target/$stem.s"
    if ! "$gcc" -E -P -std=gnu11 -D_GNU_SOURCE \
      -I"$src/include" -I"$src" -include "$src/include/autoconf.h" \
      "$src/$file" -o "$inp" 2>"$work/$target/$stem.cpp.err" ||
       ! "$gcc" -O0 -std=gnu11 -c -x c "$inp" \
       -o "$work/$target/$stem.gnu.o" 2>"$work/$target/$stem.gnu.err"; then
      blocked=$((blocked+1))
      echo "FOUR_BACKEND_BUSYBOX_TU=BLOCKED target=$target source=$file stage=GNU_preprocess_oracle"
      tail -n 3 "$work/$target/$stem.cpp.err" "$work/$target/$stem.gnu.err" 2>/dev/null || true
      continue
    fi
    if ! timeout 90s env MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 \
        "$MINIC_CC" --target="$target-linux" -S "$inp" -o "$asm" \
        2>"$work/$target/$stem.mini.err"; then
      blocked=$((blocked+1))
      first=$(grep -m 1 -E 'MINIC_BACKEND_REJECT|error:' \
        "$work/$target/$stem.mini.err" || true)
      echo "FOUR_BACKEND_BUSYBOX_TU=BLOCKED target=$target source=$file stage=MiniC detail=$first"
      continue
    fi
    if ! "$gcc" -c -x assembler "$asm" -o "$work/$target/$stem.mini.o" \
        2>"$work/$target/$stem.as.err"; then
      blocked=$((blocked+1))
      echo "FOUR_BACKEND_BUSYBOX_TU=BLOCKED target=$target source=$file stage=GNU_AS"
      tail -n 5 "$work/$target/$stem.as.err"; continue
    fi
    passed=$((passed+1))
    echo "FOUR_BACKEND_BUSYBOX_TU=PASS target=$target source=$file MiniC_GNU_AS=1"
  done
  echo "FOUR_BACKEND_BUSYBOX_FRONTIER target=$target compiled=$passed total=9 blocked=$blocked"
  if [ "$passed" -ne 9 ]; then
    echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=nine_applet_objects"
    continue
  fi
  for file in $sources; do
    stem=$(basename "$file" .c)
    test -f "$src/${file%.c}.o" || { echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=missing_Kbuild_owner file=$file"; exit 1; }
    cp "$work/$target/$stem.mini.o" "$src/${file%.c}.o"
  done
  rm -f "$src/coreutils/built-in.o" "$src/coreutils/lib.a" \
        "$src/findutils/built-in.o" "$src/findutils/lib.a" \
        "$src/busybox_unstripped" "$src/busybox"
  if ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
      CROSS_COMPILE="$prefix" busybox \
      >"$work/$target/mixed.out" 2>"$work/$target/mixed.err"); then
    echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=GNU_original_Kbuild_relink"
    # BusyBox's original Kbuild trylink captures the linker's stderr into
    # busybox_unstripped.out (make stdout), so printing only mixed.err hid
    # the FIRST actual link failure on AArch64.
    tail -n 55 "$work/$target/mixed.out" "$work/$target/mixed.err" || true
    if [ -f "$src/busybox_unstripped.out" ]; then
        tail -n 35 "$src/busybox_unstripped.out" || true
    fi
    continue
  fi
  for file in $sources; do
    stem=$(basename "$file" .c)
    if ! cmp -s "$src/${file%.c}.o" "$work/$target/$stem.mini.o"; then
      echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=GNU_overwrote_MiniC source=$file"; exit 1
    fi
  done
  cp "$src/busybox" "$work/$target/mini/busybox"
  printf 'one\nsecond\n' >"$work/$target/text"
  printf 'a:3\nb:1\nb:1\nc:2\n' >"$work/$target/columns"
  printf 'delta\nalpha\nbeta\n' >"$work/$target/words"
  if ! python3 - "$qemu" "$sysroot" "$work/$target" <<'PY'
import os,subprocess,sys
qemu,sysroot,root=sys.argv[1:]
tests=(
 ("cat",["cat",root+"/text"],b""),
 ("echo",["echo","busybox-real-project"],b""),
 ("head",["head","-n","1",root+"/text"],b""),
 ("wc",["wc","-l",root+"/text"],b""),
 ("grep",["grep","-F","second",root+"/text"],b""),
 ("cut",["cut","-d:","-f2",root+"/columns"],b""),
 ("sort",["sort",root+"/words"],b""),
 ("uniq",["uniq"],b"alpha\nbeta\nbeta\n"),
 ("tr",["tr","a-z","A-Z"],b"Abc-XYZ\n"))
for name,args,stdin in tests:
    result=[]
    for kind in ("gnu","mini"):
        p=subprocess.run([qemu,"-L",sysroot,root+"/"+kind+"/busybox"]+args,
          input=stdin,capture_output=True,timeout=35,
          env=dict(os.environ,LC_ALL="C"),check=False)
        result.append((p.returncode,p.stdout,p.stderr))
    if result[0]!=result[1] or result[0][0]!=0:
        print("FOUR_BACKEND_BUSYBOX_APPLET=BLOCKED name=%s GNU=%r MiniC=%r"%(name,*result),flush=True)
        sys.exit(1)
    print("FOUR_BACKEND_BUSYBOX_APPLET=PASS name=%s GNU_QEMU_byte_exact=1"%name,flush=True)
PY
  then
    echo "FOUR_BACKEND_BUSYBOX=BLOCKED target=$target stage=GNU_QEMU_applet_differential"
    continue
  fi
  echo "FOUR_BACKEND_BUSYBOX_MIXED=PASS target=$target MiniC_Kbuild_owners=9 GNU_remaining_owners=yes original_Kconfig_Kbuild=1 QEMU_GNU_byte_exact=1 full_MiniC=NO"

  # One *class-level* survey per ISA: no extra CI job and no changes to
  # the certified nine-owner binary. These are untouched upstream BusyBox
  # sources preprocessed under the accepted nine-applet Kconfig; they are
  # not claimed as active Kbuild owners until separately promoted.
  if [ "${FOUR_BACKEND_BUSYBOX_SWEEP:-0}" = 1 ]; then
    sweep_sources='
coreutils/basename.c coreutils/dirname.c coreutils/printf.c
coreutils/pwd.c coreutils/whoami.c coreutils/yes.c coreutils/uname.c
coreutils/nproc.c coreutils/sleep.c coreutils/env.c coreutils/printenv.c
coreutils/mkdir.c coreutils/rmdir.c coreutils/touch.c coreutils/tty.c
coreutils/hostid.c coreutils/logname.c coreutils/link.c
coreutils/unlink.c coreutils/ln.c coreutils/mv.c coreutils/cp.c
coreutils/rm.c coreutils/id.c coreutils/ls.c coreutils/tail.c
coreutils/tee.c coreutils/seq.c coreutils/chmod.c coreutils/chgrp.c
coreutils/chown.c coreutils/stat.c coreutils/split.c coreutils/date.c
coreutils/df.c coreutils/du.c coreutils/cksum.c coreutils/nl.c
coreutils/fold.c coreutils/expand.c coreutils/paste.c coreutils/readlink.c
coreutils/realpath.c coreutils/mktemp.c coreutils/od.c coreutils/sum.c
coreutils/sync.c coreutils/truncate.c coreutils/usleep.c
coreutils/who.c coreutils/md5_sha1_sum.c coreutils/shuf.c
findutils/find.c findutils/xargs.c editors/sed.c editors/awk.c
editors/cmp.c editors/diff.c networking/ping.c
'
    pass=0; cpp_fail=0; mini_fail=0; as_fail=0; total=0
    for file in $sweep_sources; do
      total=$((total+1))
      stem=${file##*/}
      stem=${stem%.c}
      input="$work/$target/sweep-$stem.i"
      output="$work/$target/sweep-$stem.s"
      if ! "$gcc" -E -P -std=gnu11 -D_GNU_SOURCE \
          -I"$src/include" -I"$src" -include "$src/include/autoconf.h" \
          "$src/$file" -o "$input" > /dev/null \
          2>"$work/$target/sweep-$stem.cpp.err"; then
        cpp_fail=$((cpp_fail+1))
        echo "FOUR_BACKEND_BUSYBOX_SWEEP_TU target=$target source=$file stage=CPP"
        continue
      fi
      if ! timeout 90s env MINIC_BACKEND_CENSUS=1 \
          "$MINIC_CC" --target="$target-linux" -S "$input" -o "$output" \
          2>"$work/$target/sweep-$stem.mini.err"; then
        mini_fail=$((mini_fail+1))
        first=$(grep -m 1 -E 'MINIC_BACKEND_REJECT|error:' \
          "$work/$target/sweep-$stem.mini.err" || true)
        echo "FOUR_BACKEND_BUSYBOX_SWEEP_TU target=$target source=$file stage=MiniC detail=$first"
        continue
      fi
      if ! "$gcc" -c -x assembler "$output" \
          -o "$work/$target/sweep-$stem.o" \
          2>"$work/$target/sweep-$stem.as.err"; then
        as_fail=$((as_fail+1))
        first=$(head -n 1 "$work/$target/sweep-$stem.as.err" || true)
        echo "FOUR_BACKEND_BUSYBOX_SWEEP_TU target=$target source=$file stage=GNU_AS detail=$first"
        continue
      fi
      pass=$((pass+1))
      echo "FOUR_BACKEND_BUSYBOX_SWEEP_TU target=$target source=$file stage=MiniC_GNU_AS_PASS"
    done
    echo "FOUR_BACKEND_BUSYBOX_SWEEP=COMPLETE target=$target scanned=$total MiniC_GNU_AS_pass=$pass CPP_blocked=$cpp_fail MiniC_blocked=$mini_fail GNU_AS_blocked=$as_fail Kbuild_owner_claim=NO"
  fi
  if [ "${FOUR_BACKEND_BUSYBOX_EXPAND:-0}" = 1 ]; then
    sh tests/external/busybox/probe-four-backend-expanded.sh \
      "$root" "$target" "$gcc" "$prefix" "$qemu" "$sysroot"
    if [ "${FOUR_BACKEND_BUSYBOX_NEXT:-0}" = 1 ]; then
      next_receipt="$work/$target/next-cohort.txt"
      FOUR_BACKEND_BUSYBOX_COHORT=next \
        sh tests/external/busybox/probe-four-backend-expanded.sh \
          "$root" "$target" "$gcc" "$prefix" "$qemu" "$sysroot" \
        | tee "$next_receipt"
      # Discovery exits zero on a blocked stage. Promotion must read the
      # exact final acceptance receipt, not the process exit or workflow green.
      # Future MiniC improvements (82/83) are allowed, regressions below
      # the certified 81 or missing QEMU checks are red.
      record=$(grep "^FOUR_BACKEND_BUSYBOX_NEXT=PASS target=$target " "$next_receipt" || true)
      minic_count=$(printf '%s\n' "$record" | sed -n 's/.* MiniC_owners=\([0-9][0-9]*\) .*/\1/p')
      case "$record" in
        *"original_Kbuild_owners=83 "*"SHA_objects_verified="*" GNU_QEMU_differential=1 full_MiniC=NO") ;;
        *) echo "FOUR_BACKEND_BUSYBOX_NEXT_ACCEPT=FAIL target=$target reason=no_real_Kbuild_QEMU_receipt" >&2; exit 1 ;;
      esac
      if [ -z "$minic_count" ] || [ "$minic_count" -lt 81 ] ||
         ! grep -qx 'FOUR_BACKEND_BUSYBOX_NEXT_RUNTIME_ACCEPT=PASS fixtures=45' "$next_receipt"; then
        echo "FOUR_BACKEND_BUSYBOX_NEXT_ACCEPT=FAIL target=$target reason=coverage_or_runtime" >&2
        exit 1
      fi
      next_targets=$((next_targets+1))
      echo "FOUR_BACKEND_BUSYBOX_NEXT_ACCEPT=PASS target=$target MiniC_owners=$minic_count original_Kbuild_owners=83"
      # Large original Kconfig/Kbuild owner cohort: 179 real objects.
      # Discovery exits zero even if the original linker or MiniC fails.
      # Strictly require a proven mixed *relinked* binary, all object SHA
      # checks, and the exact 55-case GNU/QEMU differential receipt.
      if [ "${FOUR_BACKEND_BUSYBOX_WIDE:-0}" = 1 ]; then
        wide_receipt="$work/$target/wide-cohort.txt"
        FOUR_BACKEND_BUSYBOX_COHORT=wide \
          sh tests/external/busybox/probe-four-backend-expanded.sh \
            "$root" "$target" "$gcc" "$prefix" "$qemu" "$sysroot" \
          | tee "$wide_receipt"
        wide_record=$(grep "^FOUR_BACKEND_BUSYBOX_WIDE=PASS target=$target " "$wide_receipt" || true)
        wide_mini=$(printf '%s\n' "$wide_record" | sed -n 's/.* MiniC_owners=\([0-9][0-9]*\) .*/\1/p')
        wide_sha=$(printf '%s\n' "$wide_record" | sed -n 's/.* SHA_objects_verified=\([0-9][0-9]*\) .*/\1/p')
        wide_fallback=$(printf '%s\n' "$wide_record" | sed -n 's/.* explicit_cohort_GNU_fallbacks=\([0-9][0-9]*\) .*/\1/p')
        case "$target" in
          aarch64) wide_min=170 ;;
          loongarch64) wide_min=175 ;;
        esac
        case "$wide_record" in
          *"original_Kbuild_owners=179 "*"GNU_remaining_owners=yes "*" GNU_QEMU_differential=1 full_MiniC=NO") ;;
          *) echo "FOUR_BACKEND_BUSYBOX_WIDE_ACCEPT=FAIL target=$target reason=no_real_Kbuild_QEMU_receipt" >&2; exit 1 ;;
        esac
        if [ -z "$wide_mini" ] || [ -z "$wide_sha" ] || [ -z "$wide_fallback" ] ||
           [ "$wide_mini" -lt "$wide_min" ] ||
           [ "$wide_mini" -ne "$wide_sha" ] ||
           [ "$((wide_mini + wide_fallback))" -ne 179 ] ||
           ! grep -qx 'FOUR_BACKEND_BUSYBOX_WIDE_RUNTIME_ACCEPT=PASS fixtures=58' "$wide_receipt"; then
          echo "FOUR_BACKEND_BUSYBOX_WIDE_ACCEPT=FAIL target=$target reason=coverage_objects_or_runtime" >&2
          exit 1
        fi
        # Real execution of dynamic stack Core lowering: 2 nested alloca
        # regions with a 10-arg spilled call, plus a runtime-bound VLA.
        # Fail closed on a crash, ABI mismatch or GNU result difference.
        sh tests/compiler/c0/probe-four-backend-dynamic-stack.sh \
          "$target" "$gcc" "$qemu" "$sysroot"
        wide_targets=$((wide_targets+1))
        echo "FOUR_BACKEND_BUSYBOX_WIDE_ACCEPT=PASS target=$target MiniC_owners=$wide_mini original_Kbuild_owners=179 GNU_fallbacks=$wide_fallback QEMU_cases=58 dynamic_stack_runtime=PASS"
      fi
    fi
  fi
done
if [ "${FOUR_BACKEND_BUSYBOX_NEXT:-0}" = 1 ]; then
  if [ "$next_targets" -ne 2 ]; then
    echo "FOUR_BACKEND_BUSYBOX_NEXT_ACCEPT=FAIL reason=missing_target completed=$next_targets" >&2
    exit 1
  fi
  echo "FOUR_BACKEND_BUSYBOX_NEXT_ACCEPT=PASS targets=2 original_Kbuild_owners=83 min_MiniC_owners=81 GNU_QEMU_differential=1"
fi
if [ "${FOUR_BACKEND_BUSYBOX_WIDE:-0}" = 1 ]; then
  if [ "$wide_targets" -ne 2 ]; then
    echo "FOUR_BACKEND_BUSYBOX_WIDE_ACCEPT=FAIL reason=missing_target completed=$wide_targets" >&2
    exit 1
  fi
  echo "FOUR_BACKEND_BUSYBOX_WIDE_ACCEPT=PASS targets=2 original_Kbuild_owners=179 min_AArch64_MiniC=170 min_LoongArch64_MiniC=175 GNU_QEMU_differential=1"
fi
echo "FOUR_BACKEND_BUSYBOX_DISCOVERY=COMPLETE source_pinned=1 no_all_MiniC_claim=1"

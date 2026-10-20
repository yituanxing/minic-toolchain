#!/bin/sh
# Larger *original Kbuild* cohort for AArch64 and LoongArch64.
# Called only after both frozen TinyCC and nine-owner BusyBox checks.
# Input source tree is SHA-pinned by probe-four-backend-frontier.sh.
# Exploratory until BOTH GNU/QEMU differentials pass: never a fake PASS.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
test "$#" -eq 6
root=$1
target=$2
gcc=$3
prefix=$4
qemu=$5
sysroot=$6
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$work/src"
mkdir -p "$src" "$work/gnu" "$work/mini" "$work/obj"
(cd "$root" && tar -cf - .) | (cd "$src" && tar -xf -)
# 'root' is a trusted unpacked vendor snapshot, not a modified worktree.
# Recreate config using the original Kconfig, not hand-written CPP defines.
if ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" CROSS_COMPILE="$prefix" allnoconfig \
    >"$work/config.out" 2>"$work/config.err"); then
  echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=GNU_Kconfig"
  tail -n 12 "$work/config.err"
  exit 0
fi
# 9 previously accepted plus 28 additional real applets; the candidate
# object set is fixed so a subsequent run compares the SAME source scope.
extra='BASENAME DIRNAME PRINTF PWD WHOAMI YES UNAME NPROC SLEEP ENV
PRINTENV MKDIR RMDIR TOUCH TTY HOSTID LOGNAME LINK UNLINK LN MV CP RM
ID LS TAIL TEE SEQ'
python3 - "$src/.config" $extra <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
keys = "CAT ECHO HEAD WC GREP CUT SORT UNIQ TR".split() + sys.argv[2:]
for k in keys:
    old = "# CONFIG_" + k + " is not set"
    new = "CONFIG_" + k + "=y"
    if old in s:
        s = s.replace(old, new)
    elif new not in s:
        raise SystemExit("BUSYBOX_EXPAND_CONFIG_MISSING=" + k)
open(p, "w", encoding="utf-8").write(s)
PY
if ! (cd "$src" && yes '' | make HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" oldconfig \
    >"$work/oldconfig.out" 2>"$work/oldconfig.err") ||
   ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" busybox \
    >"$work/build.out" 2>"$work/build.err"); then
  echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=GNU_original_build"
  tail -n 22 "$work/build.err" "$work/build.out" 2>/dev/null || true
  exit 0
fi
cp "$src/busybox" "$work/gnu/busybox"
# Object source ownership follows actual original C / Kbuild paths.
sources='
coreutils/cat.c coreutils/echo.c coreutils/head.c coreutils/wc.c
findutils/grep.c coreutils/cut.c coreutils/sort.c coreutils/uniq.c
coreutils/tr.c
coreutils/basename.c coreutils/dirname.c coreutils/printf.c
coreutils/pwd.c coreutils/whoami.c coreutils/yes.c coreutils/uname.c
coreutils/nproc.c coreutils/sleep.c coreutils/env.c coreutils/printenv.c
coreutils/mkdir.c coreutils/rmdir.c coreutils/touch.c coreutils/tty.c
coreutils/hostid.c coreutils/logname.c coreutils/link.c
coreutils/unlink.c coreutils/ln.c coreutils/mv.c coreutils/cp.c
coreutils/rm.c coreutils/id.c coreutils/ls.c coreutils/tail.c
coreutils/tee.c coreutils/seq.c
'
total=0
passed=0
blocked=0
for file in $sources; do
  total=$((total + 1))
  stem=${file##*/}
  stem=${stem%.c}
  input="$work/obj/$stem.i"
  output="$work/obj/$stem.s"
  if [ ! -f "$src/${file%.c}.o" ]; then
    blocked=$((blocked+1))
    echo "FOUR_BACKEND_BUSYBOX_EXPAND_TU target=$target source=$file stage=missing_Kbuild_owner"
    continue
  fi
  if ! "$gcc" -E -P -std=gnu11 -D_GNU_SOURCE \
      -I"$src/include" -I"$src" -include "$src/include/autoconf.h" \
      "$src/$file" -o "$input" 2>"$work/obj/$stem.cpp.err"; then
    blocked=$((blocked+1))
    echo "FOUR_BACKEND_BUSYBOX_EXPAND_TU target=$target source=$file stage=GNU_CPP"
    continue
  fi
  if ! timeout 90s env MINIC_BACKEND_CENSUS=1 \
      "$MINIC_CC" --target="$target-linux" -S "$input" -o "$output" \
      2>"$work/obj/$stem.mini.err"; then
    blocked=$((blocked+1))
    first=$(grep -m 1 -E 'MINIC_BACKEND_REJECT|error:' "$work/obj/$stem.mini.err" || true)
    echo "FOUR_BACKEND_BUSYBOX_EXPAND_TU target=$target source=$file stage=MiniC detail=$first"
    continue
  fi
  if ! "$gcc" -c -x assembler "$output" -o "$work/obj/$stem.o" \
      2>"$work/obj/$stem.as.err"; then
    blocked=$((blocked+1))
    first=$(head -n 1 "$work/obj/$stem.as.err" || true)
    echo "FOUR_BACKEND_BUSYBOX_EXPAND_TU target=$target source=$file stage=GNU_AS detail=$first"
    continue
  fi
  passed=$((passed+1))
  echo "FOUR_BACKEND_BUSYBOX_EXPAND_TU target=$target source=$file stage=MiniC_GNU_AS_PASS"
done
echo "FOUR_BACKEND_BUSYBOX_EXPAND_FRONTIER target=$target original_Kbuild_owners=$total MiniC_GNU_AS_pass=$passed blocked=$blocked"
if [ "$passed" -ne "$total" ]; then
  echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=selected_real_Kbuild_objects"
  exit 0
fi
# Swap actual GNU Kbuild owner .o, discard composite archives, and link.
# SHA-compare EVERY substituted object to detect silent GNU overwrites.
for file in $sources; do
  stem=${file##*/}
  stem=${stem%.c}
  cp "$work/obj/$stem.o" "$src/${file%.c}.o"
done
for dir in coreutils findutils; do
  rm -f "$src/$dir/built-in.o" "$src/$dir/lib.a"
done
rm -f "$src/busybox" "$src/busybox_unstripped"
if ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" busybox \
    >"$work/mixed.out" 2>"$work/mixed.err"); then
  echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=GNU_original_Kbuild_relink"
  tail -n 22 "$work/mixed.out" "$work/mixed.err" "$src/busybox_unstripped.out" 2>/dev/null || true
  exit 0
fi
for file in $sources; do
  stem=${file##*/}
  stem=${stem%.c}
  if ! cmp -s "$src/${file%.c}.o" "$work/obj/$stem.o"; then
    echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=GNU_overwrote_MiniC source=$file"
    exit 0
  fi
done
cp "$src/busybox" "$work/mini/busybox"
# Controlled GNU-vs-MiniC comparisons. These original applets are tested
# through the actual BusyBox dispatcher, with deterministic environment.
if ! python3 - "$qemu" "$sysroot" "$work" <<'PY'
import os, subprocess, sys
qemu, sysroot, root = sys.argv[1:]
sample = os.path.join(root, "sample")
with open(sample, "wb") as f:
    f.write(b"alpha\nbeta\nalpha\n")
cases = [
 ("cat", ["cat", sample], b""),
 ("echo", ["echo", "hello"], b""),
 ("head", ["head", "-n", "1", sample], b""),
 ("wc", ["wc", "-l", sample], b""),
 ("grep", ["grep", "-F", "beta", sample], b""),
 ("cut", ["cut", "-c", "1", sample], b""),
 ("sort", ["sort", sample], b""),
 ("uniq", ["uniq"], b"alpha\nalpha\n"),
 ("tr", ["tr", "a-z", "A-Z"], b"abc\n"),
 ("basename", ["basename", "/tmp/alpha.log", ".log"], b""),
 ("dirname", ["dirname", "/tmp/alpha.log"], b""),
 ("printf", ["printf", "%s:%04d\\n", "native", "42"], b""),
 ("pwd", ["pwd"], b""),
 ("whoami", ["whoami"], b""),
 ("uname", ["uname", "-s"], b""),
 ("nproc", ["nproc"], b""),
 ("sleep", ["sleep", "0"], b""),
 ("env", ["env", "-i", "X=42"], b""),
 ("printenv", ["printenv", "HOME"], b""),
 ("hostid", ["hostid"], b""),
 ("ls", ["ls", "-1", root], b""),
 ("tail", ["tail", "-n", "1", sample], b""),
 ("tee", ["tee"], b"hello\n"),
 ("seq", ["seq", "1", "3"], b""),
 ("id", ["id", "-u"], b""),
]
for label, argv, input_bytes in cases:
    results = []
    for mode in ("gnu", "mini"):
        p = subprocess.run(
            [qemu, "-L", sysroot, os.path.join(root, mode, "busybox")] + argv,
            input=input_bytes, capture_output=True, timeout=20,
            env=dict(os.environ, LC_ALL="C"), check=False)
        results.append((p.returncode, p.stdout, p.stderr))
    if results[0] != results[1]:
        print("FOUR_BACKEND_BUSYBOX_EXPAND_RUNTIME=FAIL applet=%s GNU=%r MINI=%r" %
              (label, results[0], results[1]), flush=True)
        sys.exit(1)
    print("FOUR_BACKEND_BUSYBOX_EXPAND_RUNTIME=PASS applet=%s" % label, flush=True)
print("FOUR_BACKEND_BUSYBOX_EXPAND_RUNTIME_ACCEPT=PASS fixtures=%d" % len(cases), flush=True)
PY
then
  echo "FOUR_BACKEND_BUSYBOX_EXPAND=BLOCKED target=$target stage=GNU_QEMU_differential"
  exit 0
fi
echo "FOUR_BACKEND_BUSYBOX_EXPAND=PASS target=$target original_Kbuild_owners=$total MiniC_owners=$passed GNU_remaining_owners=yes SHA_objects_verified=$passed GNU_QEMU_differential=1 full_MiniC=NO"

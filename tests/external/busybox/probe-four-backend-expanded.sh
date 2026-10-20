#!/bin/sh
# Larger *original Kbuild* cohort for AArch64 and LoongArch64.
# Called only after both frozen TinyCC and nine-owner BusyBox checks.
# Input source tree is SHA-pinned by probe-four-backend-frontier.sh.
# Exploratory until BOTH GNU/QEMU differentials pass: never a fake PASS.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
cohort=${FOUR_BACKEND_BUSYBOX_COHORT:-baseline}
case "$cohort" in
  baseline) tag=FOUR_BACKEND_BUSYBOX_EXPAND ;;
  next) tag=FOUR_BACKEND_BUSYBOX_NEXT ;;
  wide) tag=FOUR_BACKEND_BUSYBOX_WIDE ;;
  *) echo "BUSYBOX_INVALID_COHORT=$cohort" >&2; exit 1 ;;
esac
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
  echo "${tag}=BLOCKED target=$target stage=GNU_Kconfig"
  tail -n 12 "$work/config.err"
  exit 0
fi
# 9 previously accepted plus 28 additional real applets; the candidate
# object set is fixed so a subsequent run compares the SAME source scope.
extra='BASENAME DIRNAME PRINTF PWD WHOAMI YES UNAME NPROC SLEEP ENV
PRINTENV MKDIR RMDIR TOUCH TTY HOSTID LOGNAME LINK UNLINK LN MV CP RM
ID LS TAIL TEE SEQ'
if [ "$cohort" != baseline ]; then
  extra="$extra
CHGRP CHMOD CHOWN CHROOT CKSUM COMM DATE DD
DF DOS2UNIX DU EXPR FACTOR FALSE FOLD INSTALL
MD5SUM MKFIFO MKNOD MKTEMP NICE NL NOHUP OD
PASTE READLINK REALPATH SHRED SHUF SPLIT STAT SUM
SYNC TAC TEST TIMEOUT TRUE TRUNCATE TSORT USLEEP
UUDECODE UUENCODE FIND XARGS SED CMP
"
fi
# The baseline GNU cross toolchains have no target crypt() library:
# both wide original GNU builds failed in libbb/pw_encrypt.o. Keep the
# password-management applets as a separate, explicit target-sysroot
# prerequisite rather than misreporting them as MiniC compiler defects.
# GETTY is retained; the password crypto consumers are excluded.
if [ "$cohort" = wide ]; then
  extra="$extra
AR BZIP2 CPIO DPKG_DEB GZIP TAR UNZIP CLEAR
RESET RESIZE CHVT FGCONSOLE OPENVT INIT HALT GETTY
ASCII BEEP CROND CRONTAB DEVMEM HEXEDIT INOTIFYD LESS
MAN MICROCOM MT READAHEAD RFKILL RUNLEVEL STRINGS TIME
TREE TS TTYSIZE ARP ARPING BRCTL DNSD HOSTNAME
IFCONFIG IP IPCALC NC NETSTAT NSLOOKUP NTPD PING
PSCAN ROUTE TELNET TFTP TRACEROUTE WGET WHOIS ZCIP
FREE FUSER IOSTAT KILL LSOF MPSTAT PGREP PIDOF
PMAP PS PSTREE PWDX BB_SYSCTL TOP UPTIME WATCH
BLKID CAL CHRT DMESG EJECT FALLOCATE FLOCK GETOPT
HEXDUMP HWCLOCK IONICE IPCRM IPCS LAST LOSETUP LSPCI
LSUSB MESG MOUNT MOUNTPOINT NOLOGIN RENICE REV SETARCH
SETSID TASKSET UMOUNT WALL ASH
"
fi
if ! python3 - "$src/.config" $extra <<'PY'
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
then
  # Exploratory third cohort must never turn the already certified two
  # real Kbuild/QEMU cohorts red because an upstream Kconfig option is
  # absent or conditional. Report the stage explicitly, without success.
  if [ "$cohort" = wide ]; then
    echo "${tag}=BLOCKED target=$target stage=original_Kconfig_missing_option"
    exit 0
  fi
  exit 1
fi
if ! (cd "$src" && yes '' | make HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" oldconfig \
    >"$work/oldconfig.out" 2>"$work/oldconfig.err") ||
   ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" busybox \
    >"$work/build.out" 2>"$work/build.err"); then
  echo "${tag}=BLOCKED target=$target stage=GNU_original_build"
  # BusyBox's Kbuild trylink captures the *real* GNU linker stderr in
  # busybox_unstripped.out. stderr/build.err ends with harmless GCC warnings
  # and hides the first undefined symbol unless this file is printed.
  if [ -f "$src/busybox_unstripped.out" ]; then
    grep -E -m 16 'undefined reference|multiple definition|relocation|collect2: error|ld: error|cannot find' "$src/busybox_unstripped.out" || true
    tail -n 26 "$src/busybox_unstripped.out" || true
  fi
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
if [ "$cohort" != baseline ]; then
  sources="$sources
coreutils/chgrp.c coreutils/chmod.c coreutils/chown.c coreutils/chroot.c
coreutils/cksum.c coreutils/comm.c coreutils/date.c coreutils/dd.c
coreutils/df.c coreutils/dos2unix.c coreutils/du.c coreutils/expr.c
coreutils/factor.c coreutils/false.c coreutils/fold.c coreutils/install.c
coreutils/md5_sha1_sum.c coreutils/mkfifo.c coreutils/mknod.c coreutils/mktemp.c
coreutils/nice.c coreutils/nl.c coreutils/nohup.c coreutils/od.c
coreutils/paste.c coreutils/readlink.c coreutils/realpath.c coreutils/shred.c
coreutils/shuf.c coreutils/split.c coreutils/stat.c coreutils/sum.c
coreutils/sync.c coreutils/tac.c coreutils/test.c coreutils/timeout.c
coreutils/true.c coreutils/truncate.c coreutils/tsort.c coreutils/usleep.c
coreutils/uudecode.c coreutils/uuencode.c findutils/find.c findutils/xargs.c
editors/sed.c editors/cmp.c
"
fi
# Five original C files were not Kbuild owners under this real .config:
# miscutils/{readahead,runlevel}.c and util-linux/{last,nologin,wall}.c.
# Keep upstream Kconfig unchanged, but do not invent object ownership.
if [ "$cohort" = wide ]; then
  sources="$sources
archival/ar.c archival/bzip2.c archival/cpio.c archival/dpkg_deb.c
archival/gzip.c archival/tar.c archival/unzip.c console-tools/clear.c
console-tools/reset.c console-tools/resize.c console-tools/chvt.c console-tools/fgconsole.c
console-tools/openvt.c init/init.c init/halt.c loginutils/getty.c
miscutils/ascii.c miscutils/beep.c miscutils/crond.c miscutils/crontab.c
miscutils/devmem.c miscutils/hexedit.c miscutils/inotifyd.c miscutils/less.c
miscutils/man.c miscutils/microcom.c miscutils/mt.c miscutils/rfkill.c
miscutils/strings.c miscutils/time.c miscutils/tree.c miscutils/ts.c
miscutils/ttysize.c networking/arp.c networking/arping.c networking/brctl.c
networking/dnsd.c networking/hostname.c networking/ifconfig.c networking/ip.c
networking/ipcalc.c networking/nc.c networking/netstat.c networking/nslookup.c
networking/ntpd.c networking/ping.c networking/pscan.c networking/route.c
networking/telnet.c networking/tftp.c networking/traceroute.c networking/wget.c
networking/whois.c networking/zcip.c procps/free.c procps/fuser.c
procps/iostat.c procps/kill.c procps/lsof.c procps/mpstat.c
procps/pgrep.c procps/pidof.c procps/pmap.c procps/ps.c
procps/pstree.c procps/pwdx.c procps/sysctl.c procps/top.c
procps/uptime.c procps/watch.c util-linux/blkid.c util-linux/cal.c
util-linux/chrt.c util-linux/dmesg.c util-linux/eject.c util-linux/fallocate.c
util-linux/flock.c util-linux/getopt.c util-linux/hexdump.c util-linux/hwclock.c
util-linux/ionice.c util-linux/ipcrm.c util-linux/ipcs.c util-linux/losetup.c
util-linux/lspci.c util-linux/lsusb.c util-linux/mesg.c util-linux/mount.c
util-linux/mountpoint.c util-linux/renice.c util-linux/rev.c util-linux/setarch.c
util-linux/setsid.c util-linux/taskset.c util-linux/umount.c shell/ash.c
"
fi
total=0
passed=0
blocked=0
: > "$work/qualified.list"
: > "$work/unsupported.list"
for file in $sources; do
  total=$((total + 1))
  stem=${file##*/}
  stem=${stem%.c}
  input="$work/obj/$stem.i"
  output="$work/obj/$stem.s"
  if [ ! -f "$src/${file%.c}.o" ]; then
    blocked=$((blocked+1))
    printf '%s\n' "$file" >> "$work/unsupported.list"
    echo "${tag}_TU target=$target source=$file stage=missing_Kbuild_owner"
    continue
  fi
  if ! "$gcc" -E -P -std=gnu11 -D_GNU_SOURCE \
      -I"$src/include" -I"$src" -include "$src/include/autoconf.h" \
      "$src/$file" -o "$input" 2>"$work/obj/$stem.cpp.err"; then
    blocked=$((blocked+1))
    printf '%s\n' "$file" >> "$work/unsupported.list"
    echo "${tag}_TU target=$target source=$file stage=GNU_CPP"
    continue
  fi
  if ! timeout 90s env MINIC_BACKEND_CENSUS=1 \
      "$MINIC_CC" --target="$target-linux" -S "$input" -o "$output" \
      2>"$work/obj/$stem.mini.err"; then
    blocked=$((blocked+1))
    printf '%s\n' "$file" >> "$work/unsupported.list"
    first=$(grep -m 1 -E 'MINIC_BACKEND_REJECT|error:' "$work/obj/$stem.mini.err" || true)
    echo "${tag}_TU target=$target source=$file stage=MiniC detail=$first"
    # The generic M1 failure alone is not actionable. On a handful of
    # representative real TU failures, reuse the existing backend whitebox
    # trace to report precise Core kind / backend guard and line. This is
    # diagnostic only; it cannot promote an unsupported object to PASS.
    if [ "$cohort" = wide ]; then
      case "$file" in
        coreutils/test.c|archival/gzip.c|init/init.c|miscutils/crond.c|networking/netstat.c|procps/top.c|procps/ps.c|shell/ash.c)
          timeout 90s env MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 \
            "$MINIC_CC" --target="$target-linux" -S "$input" \
            -o "$work/obj/$stem.trace.s" \
            2>"$work/obj/$stem.trace.err" || true
          grep -E -m 8 '^MINIC_BACKEND_REJECT|^MINIC_M1_FRONTIER|^MINIC_BACKEND_CENSUS' \
            "$work/obj/$stem.trace.err" | while IFS= read -r trace_line; do
              echo "${tag}_TRACE target=$target source=$file $trace_line"
            done || true
          ;;
      esac
    fi
    continue
  fi
  if ! "$gcc" -c -x assembler "$output" -o "$work/obj/$stem.o" \
      2>"$work/obj/$stem.as.err"; then
    blocked=$((blocked+1))
    printf '%s\n' "$file" >> "$work/unsupported.list"
    first=$(head -n 1 "$work/obj/$stem.as.err" || true)
    echo "${tag}_TU target=$target source=$file stage=GNU_AS detail=$first"
    continue
  fi
  # A successfully assembled object is not necessarily linkable. The
  # compiler can leave an unresolved internal inline specialization, which
  # GNU ld must *never* be asked to silently resolve. Census all objects
  # before the original Kbuild object swap, using the target's real nm.
  if [ "$cohort" != baseline ]; then
    if ! "${prefix}nm" -u "$work/obj/$stem.o" > "$work/obj/$stem.undefined"; then
      echo "${tag}=BLOCKED target=$target stage=GNU_NM source=$file"
      exit 0
    fi
    if grep -q '__minic_inline_spec_' "$work/obj/$stem.undefined"; then
      blocked=$((blocked+1))
      printf '%s\n' "$file" >> "$work/unsupported.list"
      internal=$(grep -m1 '__minic_inline_spec_' "$work/obj/$stem.undefined" || true)
      echo "${tag}_TU target=$target source=$file stage=UNRESOLVED_MINIC_INLINE detail=$internal"
      continue
    fi
  fi
  passed=$((passed+1))
  printf '%s\n' "$file" >> "$work/qualified.list"
  echo "${tag}_TU target=$target source=$file stage=MiniC_GNU_AS_PASS"
done
echo "${tag}_FRONTIER target=$target original_Kbuild_owners=$total MiniC_GNU_AS_pass=$passed blocked=$blocked"
if [ "$blocked" -ne 0 ]; then
  if [ "$cohort" = baseline ]; then
    echo "${tag}=BLOCKED target=$target stage=selected_real_Kbuild_objects"
    exit 0
  fi
  # Original GNU Kbuild retains *only* observed, explicit fallbacks.
  # The former 82/83 first-reject census proved test.c requires an
  # unsupported Core instruction/ABI on BOTH new ISAs.
  # First complete dual-ISA 184-source census (CI #39417252989) found
  # exactly 23 AArch64 / 21 LoongArch64 known GNU fallbacks. The five
  # Kconfig-inactive C paths are omitted from this 179-*active-owner* scope.
  # A formerly supported owner may improve, but never silently regress to
  # an unreviewed GNU fallback or a missing Kbuild source.
  if [ "$cohort" = wide ]; then
    approved_fallbacks='
coreutils/df.c
coreutils/test.c
archival/gzip.c
init/init.c
loginutils/getty.c
miscutils/ascii.c
miscutils/crond.c
miscutils/devmem.c
miscutils/hexedit.c
miscutils/less.c
networking/arping.c
networking/netstat.c
networking/ntpd.c
networking/traceroute.c
networking/wget.c
procps/iostat.c
procps/mpstat.c
procps/ps.c
procps/pstree.c
procps/top.c
util-linux/ipcrm.c
util-linux/ipcs.c
shell/ash.c
'
    printf '%s\n' $approved_fallbacks > "$work/approved_fallbacks.txt"
    unexpected=$(grep -v -x -f "$work/approved_fallbacks.txt" "$work/unsupported.list" || true)
  else
    unexpected=$(grep -v -x -e 'coreutils/test.c' -e 'coreutils/df.c' "$work/unsupported.list" || true)
  fi
  if [ -n "$unexpected" ]; then
    echo "${tag}=BLOCKED target=$target stage=unapproved_fallback details=$unexpected"
    exit 0
  fi
  echo "${tag}_GNU_FALLBACK=KNOWN target=$target owners=$blocked files=$(tr '\n' ',' < "$work/unsupported.list")"
fi
# Swap actual GNU Kbuild owner .o, discard composite archives, and link.
# SHA-compare EVERY substituted object to detect silent GNU overwrites.
while IFS= read -r file; do
  stem=${file##*/}
  stem=${stem%.c}
  cp "$work/obj/$stem.o" "$src/${file%.c}.o"
done < "$work/qualified.list"
# Every directory with a replaced Kbuild source owner must be rebuilt.
# The wide cohort includes shell/init/networking/archival/procps etc.; the
# earlier hardcoded 3 directories would retain stale GNU archives.
cut -d/ -f1 "$work/qualified.list" | sort -u | while IFS= read -r dir; do
  rm -f "$src/$dir/built-in.o" "$src/$dir/lib.a"
done
rm -f "$src/busybox" "$src/busybox_unstripped"
if ! (cd "$src" && make -j2 HOSTCC=cc CC="$gcc" \
    CROSS_COMPILE="$prefix" busybox \
    >"$work/mixed.out" 2>"$work/mixed.err"); then
  echo "${tag}=BLOCKED target=$target stage=GNU_original_Kbuild_relink"
  tail -n 22 "$work/mixed.out" "$work/mixed.err" "$src/busybox_unstripped.out" 2>/dev/null || true
  exit 0
fi
while IFS= read -r file; do
  stem=${file##*/}
  stem=${stem%.c}
  if ! cmp -s "$src/${file%.c}.o" "$work/obj/$stem.o"; then
    echo "${tag}=BLOCKED target=$target stage=GNU_overwrote_MiniC source=$file"
    exit 0
  fi
done < "$work/qualified.list"
if cmp -s "$work/gnu/busybox" "$src/busybox"; then
  echo "${tag}=BLOCKED target=$target stage=GNU_baseline_binary_unchanged"
  exit 0
fi
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
tag = ("FOUR_BACKEND_BUSYBOX_WIDE" if
       os.environ.get("FOUR_BACKEND_BUSYBOX_COHORT") == "wide" else
       "FOUR_BACKEND_BUSYBOX_NEXT" if
       os.environ.get("FOUR_BACKEND_BUSYBOX_COHORT") == "next"
       else "FOUR_BACKEND_BUSYBOX_EXPAND")
if os.environ.get("FOUR_BACKEND_BUSYBOX_COHORT") in ("next", "wide"):
    cases.extend([
      ("cksum", ["cksum", sample], b""),
      ("comm", ["comm", sample, sample], b""),
      ("expr", ["expr", "7", "+", "35"], b""),
      ("factor", ["factor", "21", "35"], b""),
      ("false", ["false"], b""),
      ("true", ["true"], b""),
      ("fold", ["fold", "-w", "3", sample], b""),
      ("md5sum", ["md5sum", sample], b""),
      ("nl", ["nl", sample], b""),
      ("od", ["od", "-An", "-tx1", sample], b""),
      ("paste", ["paste", sample, sample], b""),
      ("realpath", ["realpath", sample], b""),
      ("stat", ["stat", sample], b""),
      ("sum", ["sum", sample], b""),
      ("tac", ["tac", sample], b""),
      ("test", ["test", "7", "-eq", "7"], b""),
      ("tsort", ["tsort"], b"a b\nb c\n"),
      ("sed", ["sed", "s/alpha/ALPHA/g", sample], b""),
      ("cmp", ["cmp", sample, sample], b""),
      ("find", ["find", root, "-maxdepth", "0"], b""),
    ])
if os.environ.get("FOUR_BACKEND_BUSYBOX_COHORT") == "wide":
    cases.extend([
      ("ash", ["ash", "-c", "printf '%s\\n' shell-ok"], b""),
      ("gzip", ["gzip", "-c", sample], b""),
      ("hostname", ["hostname"], b""),
      ("rev", ["rev"], b"abc\ndef\n"),
      ("hexdump", ["hexdump", "-C", sample], b""),
      ("strings", ["strings", sample], b""),
      ("tree", ["tree", "-a", "-i", root], b""),
      ("cal", ["cal", "1", "2026"], b""),
      ("getopt", ["getopt", "-o", "ab:", "--", "-a", "-b", "42"], b""),
      ("mountpoint", ["mountpoint", "-q", root], b""),
    ])
for label, argv, input_bytes in cases:
    results = []
    for mode in ("gnu", "mini"):
        p = subprocess.run(
            [qemu, "-L", sysroot, os.path.join(root, mode, "busybox")] + argv,
            input=input_bytes, capture_output=True, timeout=20,
            env=dict(os.environ, LC_ALL="C"), check=False)
        results.append((p.returncode, p.stdout, p.stderr))
    if results[0] != results[1] or results[0][0] in (126, 127):
        print("%s_RUNTIME=FAIL applet=%s GNU=%r MINI=%r" %
              (tag, label, results[0], results[1]), flush=True)
        sys.exit(1)
    print("%s_RUNTIME=PASS applet=%s" % (tag, label), flush=True)
print("%s_RUNTIME_ACCEPT=PASS fixtures=%d" % (tag, len(cases)), flush=True)
PY
then
  echo "${tag}=BLOCKED target=$target stage=GNU_QEMU_differential"
  exit 0
fi
echo "${tag}=PASS target=$target original_Kbuild_owners=$total MiniC_owners=$passed GNU_remaining_owners=yes SHA_objects_verified=$passed explicit_cohort_GNU_fallbacks=$blocked GNU_QEMU_differential=1 full_MiniC=NO"

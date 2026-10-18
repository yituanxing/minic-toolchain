#!/bin/sh
# BusyBox 1.36.1 real C application discovery, no upstream source patches.
# GCC provides the native applet/runtime oracle. MiniC x86_64 first
# compiles unmodified applet TUs against upstream-generated Kconfig headers.
set -eu
: "${MINIC_CC:?must point to built minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP BusyBox requires x86-64"; exit 0;; esac
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/src"
curl -fsSL --retry 3 "https://github.com/mirror/busybox/archive/$commit.tar.gz" -o "$work/busybox.tar.gz"
tar -xzf "$work/busybox.tar.gz" -C "$work/src" --strip-components=1
src="$work/src"
test "$(git hash-object "$src/Makefile")" = fa8cb761443388730372eaf260e7ca1a0235bc43
test "$(git hash-object "$src/coreutils/cat.c")" = 4b3414941e87a47bd639bc481ab9f3fa0045ea80
test "$(git hash-object "$src/coreutils/echo.c")" = 44b2eb5d05fac12374b5f8692990014087cfda01
echo "BUSYBOX_X86_64_SOURCE=PASS version=1.36.1 commit=$commit original=unchanged"

# Use upstream Kconfig and Kbuild; alter .config only, never application C.
if ! (cd "$src" && make -j2 allnoconfig CC="$HOST_CC" \
     >"$work/config.out" 2>"$work/config.err"); then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=allnoconfig" >&2
  tail -n 30 "$work/config.err" >&2
  exit 1
fi
python3 - "$src/.config" <<'PY'
import sys
p = sys.argv[1]
s = open(p,encoding="utf-8").read()
for key in ["CAT","ECHO"]:
    old = "# CONFIG_" + key + " is not set"
    new = "CONFIG_" + key + "=y"
    if old in s:
        s = s.replace(old,new)
    elif new not in s:
        raise SystemExit("missing CONFIG_" + key)
open(p,"w",encoding="utf-8").write(s)
PY
if ! (cd "$src" && yes '' | make CC="$HOST_CC" oldconfig \
     >"$work/oldconfig.out" 2>"$work/oldconfig.err"); then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=oldconfig" >&2
  tail -n 30 "$work/oldconfig.err" >&2
  exit 1
fi
if ! (cd "$src" && make -j2 CC="$HOST_CC" busybox \
     >"$work/build.out" 2>"$work/build.err"); then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=GNU-build" >&2
  tail -n 55 "$work/build.err" "$work/build.out" >&2
  exit 1
fi
# With an allnoconfig baseline, --list is an optional BusyBox
# dispatcher feature and may be compiled out. Verify Kconfig and invoke
# the enabled real applets instead of relying on the optional --list.
if ! grep -qx 'CONFIG_CAT=y' "$src/.config" ||
   ! grep -qx 'CONFIG_ECHO=y' "$src/.config"; then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=applet-config" >&2
  grep -E '^CONFIG_(CAT|ECHO)=' "$src/.config" >&2 || true
  exit 1
fi
printf 'one\nsecond\n' >"$work/fixture.txt"
if ! "$src/busybox" cat "$work/fixture.txt" >"$work/cat.out" ||
   ! cmp -s "$work/fixture.txt" "$work/cat.out" ||
   ! "$src/busybox" echo busybox-real-project >"$work/echo.out" ||
   ! grep -qx busybox-real-project "$work/echo.out"; then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=applet-runtime" >&2
  exit 1
fi
echo "BUSYBOX_X86_64_BASELINE=PASS GCC_build=busybox applets=cat,echo runtime=byte-exact"
passed=0
blocked=0
for file in coreutils/cat.c coreutils/echo.c; do
    stem=$(basename "$file" .c)
    if ! "$HOST_CC" -E -P -std=gnu11 -D_GNU_SOURCE \
       -I"$src/include" -I"$src" -include "$src/include/autoconf.h" \
       "$src/$file" -o "$work/$stem.i" >"$work/$stem.cpp.out" 2>"$work/$stem.cpp.err"; then
        echo "BUSYBOX_X86_64_BLOCKER=HOST_CPP source=$file" >&2
        sed -n '1,24p' "$work/$stem.cpp.err" >&2
        blocked=$((blocked + 1))
        continue
    fi
    echo "BUSYBOX_X86_64_INPUT=PASS source=$file lines=$(wc -l < "$work/$stem.i")"
    if ! MINIC_X86_CALL_TRACE=1 CORE_FAST_TRACE=1 "$MINIC_CC" \
       --target=x86_64-linux -S "$work/$stem.i" -o "$work/$stem.s" \
       >"$work/$stem.minic.out" 2>"$work/$stem.minic.err"; then
        echo "BUSYBOX_X86_64_BLOCKER=MiniC source=$file stage=compiler" >&2
        grep -E 'X86_CALL_GATE|X86_CALL_ARG|FRONTEND_LOOKUP_DETAIL' "$work/$stem.minic.err" >&2 || true
        sed -n '1,35p' "$work/$stem.minic.err" >&2
        blocked=$((blocked + 1))
        continue
    fi
    if ! "$HOST_CC" -c -x assembler "$work/$stem.s" -o "$work/$stem.minic.o" \
       >"$work/$stem.as.out" 2>"$work/$stem.as.err"; then
        echo "BUSYBOX_X86_64_BLOCKER=GNU_AS source=$file stage=assembler" >&2
        sed -n '1,35p' "$work/$stem.as.err" >&2
        blocked=$((blocked + 1))
        continue
    fi
    passed=$((passed + 1))
    echo "BUSYBOX_X86_64_TU=PASS source=$file object=MiniC"
done
echo "BUSYBOX_X86_64_FRONTIER=SCOPED_TU objects=$passed total=2 blocked=$blocked full_MiniC_busybox=NOT_TESTED"
if test "$blocked" -ne 0 && test "$(printenv BUSYBOX_X86_64_DISCOVERY || true)" != 1; then exit 1; fi

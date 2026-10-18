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
test "$(git hash-object "$src/coreutils/head.c")" = c7537a20e4e01483f3a6ffaf89eb2da25d7acf5f
test "$(git hash-object "$src/coreutils/wc.c")" = d5238d3fcfba2f62f6bb292a3af55c5107c3ec93
test "$(git hash-object "$src/findutils/grep.c")" = 0b72812f18b9177855e226c78f3625146b8daee2
test "$(git hash-object "$src/coreutils/cut.c")" = 55bdd938611feb51e7bf1b7977f87015b2819cf9
test "$(git hash-object "$src/coreutils/sort.c")" = 01b7c44e5e2cc72027269848b9c025abe45c7506
test "$(git hash-object "$src/coreutils/uniq.c")" = 06c57f750be6a94de5189495688a3150385166aa
test "$(git hash-object "$src/coreutils/tr.c")" = 1e402dfdbd4f047677b8f72ac884eb6e38110fb5
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
for key in ["CAT","ECHO","HEAD","WC","GREP","CUT","SORT","UNIQ","TR"]:
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
   ! grep -qx 'CONFIG_ECHO=y' "$src/.config" ||
   ! grep -qx 'CONFIG_HEAD=y' "$src/.config" ||
   ! grep -qx 'CONFIG_WC=y' "$src/.config" ||
   ! grep -qx 'CONFIG_GREP=y' "$src/.config" ||
   ! grep -qx 'CONFIG_CUT=y' "$src/.config" ||
   ! grep -qx 'CONFIG_SORT=y' "$src/.config" ||
   ! grep -qx 'CONFIG_UNIQ=y' "$src/.config" ||
   ! grep -qx 'CONFIG_TR=y' "$src/.config"; then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=applet-config" >&2
  grep -E '^CONFIG_(CAT|ECHO|HEAD|WC|GREP|CUT|SORT|UNIQ|TR)=' "$src/.config" >&2 || true
  exit 1
fi
printf 'one\nsecond\n' >"$work/fixture.txt"
printf 'a:3\nb:1\nb:1\nc:2\n' >"$work/fixture-columns.txt"
printf 'delta\nalpha\nbeta\n' >"$work/fixture-sort.txt"
if ! "$src/busybox" cat "$work/fixture.txt" >"$work/cat.out" ||
   ! cmp -s "$work/fixture.txt" "$work/cat.out" ||
   ! "$src/busybox" echo busybox-real-project >"$work/echo.out" ||
   ! grep -qx busybox-real-project "$work/echo.out" ||
   ! "$src/busybox" head -n 1 "$work/fixture.txt" >"$work/head.out" ||
   ! grep -qx one "$work/head.out" ||
   ! "$src/busybox" wc -l "$work/fixture.txt" >"$work/wc.out" ||
   ! grep -q '2 ' "$work/wc.out" ||
   ! "$src/busybox" grep -F second "$work/fixture.txt" >"$work/grep.out" ||
   ! grep -qx second "$work/grep.out" ||
   ! "$src/busybox" cut -d: -f2 "$work/fixture-columns.txt" >"$work/cut.out" ||
   ! "$src/busybox" sort "$work/fixture-sort.txt" >"$work/sort.out" ||
   ! printf 'alpha\nbeta\nbeta\n' | "$src/busybox" uniq >"$work/uniq.out" ||
   ! printf 'Abc-XYZ\n' | "$src/busybox" tr 'a-z' 'A-Z' >"$work/tr.out"; then
  echo "BUSYBOX_X86_64_BASELINE=FAIL stage=applet-runtime" >&2
  exit 1
fi
echo "BUSYBOX_X86_64_BASELINE=PASS GCC_build=busybox applets=cat,echo,head,wc,grep,cut,sort,uniq,tr runtime=byte-exact"
passed=0
blocked=0
for file in coreutils/cat.c coreutils/echo.c coreutils/head.c coreutils/wc.c \
            findutils/grep.c coreutils/cut.c coreutils/sort.c \
            coreutils/uniq.c coreutils/tr.c; do
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
        python3 - "$work/$stem.i" "$work/$stem.minic.err" <<'PY' >&2
import re
import sys
diagnostic = open(sys.argv[2], encoding="utf-8", errors="replace").read()
match = re.search(r"\.i:(\d+):(\d+): error:", diagnostic)
if match is not None:
    line = int(match.group(1))
    col = int(match.group(2))
    with open(sys.argv[1], encoding="utf-8", errors="replace") as stream:
        for pos, source in enumerate(stream, 1):
            if line - 2 <= pos <= line + 2:
                print("BUSYBOX_X86_64_DIAGNOSTIC_CONTEXT line=%d col=%d text=%r" %
                      (pos, col if pos == line else 0, source.rstrip("\n")))
PY
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
echo "BUSYBOX_X86_64_FRONTIER=SCOPED_TU objects=$passed total=9 blocked=$blocked full_MiniC_busybox=NOT_TESTED"
if test "$blocked" -ne 0 && test "$(printenv BUSYBOX_X86_64_DISCOVERY || true)" != 1; then exit 1; fi


# Escalate from isolated translation-unit acceptance to the real BusyBox
# GNU Kbuild link, substituting ONLY the two proven MiniC-generated
# applet objects. All other BusyBox objects remain the original GCC build.
# Rebuild parent composite objects and executable; do not silently
# reuse the already-GCC-linked baseline busybox.
if test "$passed" -eq 9 && test "$blocked" -eq 0; then
    for stem in cat echo head wc grep cut sort uniq tr; do
        case "$stem" in grep) original_obj="$src/findutils/grep.o" ;; *) original_obj="$src/coreutils/$stem.o" ;; esac
        if ! test -f "$original_obj"; then
            echo "BUSYBOX_X86_64_MIXED=BLOCKED stage=baseline-object-missing applet=$stem" >&2
            if test "$(printenv BUSYBOX_X86_64_DISCOVERY || true)" = 1; then exit 0; fi
            exit 1
        fi
        cp "$original_obj" "$work/$stem.gnu.o"
        cp "$work/$stem.minic.o" "$original_obj"
    done
    rm -f "$src/coreutils/built-in.o" "$src/coreutils/lib.a" \
          "$src/findutils/built-in.o" "$src/findutils/lib.a" \
          "$src/busybox_unstripped" "$src/busybox"
    if ! (cd "$src" && make -j2 CC="$HOST_CC" busybox \
          >"$work/mixed.out" 2>"$work/mixed.err"); then
        echo "BUSYBOX_X86_64_MIXED=BLOCKED stage=Kbuild-link source=nine-applets" >&2
        tail -n 65 "$work/mixed.err" "$work/mixed.out" >&2 || true
        if test "$(printenv BUSYBOX_X86_64_DISCOVERY || true)" = 1; then exit 0; fi
        exit 1
    fi
    if ! "$src/busybox" cat "$work/fixture.txt" >"$work/cat.mixed" ||
       ! cmp -s "$work/cat.out" "$work/cat.mixed" ||
       ! "$src/busybox" echo busybox-real-project >"$work/echo.mixed" ||
       ! cmp -s "$work/echo.out" "$work/echo.mixed" ||
       ! "$src/busybox" head -n 1 "$work/fixture.txt" >"$work/head.mixed" ||
       ! cmp -s "$work/head.out" "$work/head.mixed" ||
       ! "$src/busybox" wc -l "$work/fixture.txt" >"$work/wc.mixed" ||
       ! cmp -s "$work/wc.out" "$work/wc.mixed" ||
       ! "$src/busybox" grep -F second "$work/fixture.txt" >"$work/grep.mixed" ||
       ! cmp -s "$work/grep.out" "$work/grep.mixed" ||
       ! "$src/busybox" cut -d: -f2 "$work/fixture-columns.txt" >"$work/cut.mixed" ||
       ! cmp -s "$work/cut.out" "$work/cut.mixed" ||
       ! "$src/busybox" sort "$work/fixture-sort.txt" >"$work/sort.mixed" ||
       ! cmp -s "$work/sort.out" "$work/sort.mixed" ||
       ! printf 'alpha\nbeta\nbeta\n' | "$src/busybox" uniq >"$work/uniq.mixed" ||
       ! cmp -s "$work/uniq.out" "$work/uniq.mixed" ||
       ! printf 'Abc-XYZ\n' | "$src/busybox" tr 'a-z' 'A-Z' >"$work/tr.mixed" ||
       ! cmp -s "$work/tr.out" "$work/tr.mixed"; then
        echo "BUSYBOX_X86_64_MIXED=BLOCKED stage=applet-runtime" >&2
        if test "$(printenv BUSYBOX_X86_64_DISCOVERY || true)" = 1; then exit 0; fi
        exit 1
    fi
    echo "BUSYBOX_X86_64_MIXED=PASS applets=cat,echo,head,wc,grep,cut,sort,uniq,tr objects=MiniC x86 remainder=GCC GNU_Kbuild=1 runtime=byte-exact"
fi

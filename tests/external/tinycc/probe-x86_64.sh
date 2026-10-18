#!/bin/sh
# Real TinyCC 0.9.27 first TU pressure; NOT a full TinyCC acceptance.
set -eu
: "${MINIC_CC:?MINIC_CC must point to native minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP TinyCC x86-64 needs native host"; exit 0;; esac
commit=d348a9a51d32cece842b7885d27a411436d7887b
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/src"
curl -fsSL --retry 3 "https://github.com/TinyCC/tinycc/archive/$commit.tar.gz" -o "$work/tinycc.tar.gz"
tar -xzf "$work/tinycc.tar.gz" -C "$work/src" --strip-components=1
src="$work/src"
test "$(git hash-object "$src/tcc.c")" = cd887d17670dfecf0f056bb49048d3780848edab
test "$(git hash-object "$src/tcc.h")" = cd679732c40ae05e79ee5b74d2b90eaa7f944b6e
test "$(git hash-object "$src/configure")" = 1ee3acb8c944ccc5083d6ce0df9d39d7c483047d
test "$(cat "$src/VERSION")" = 0.9.27
echo "TINYCC_X86_64_SOURCE=PASS version=0.9.27 sha=$commit"
if ! (cd "$src" && ./configure --cc="$HOST_CC" >"$work/configure.out" 2>"$work/configure.err"); then
  echo "TINYCC_X86_64_BASELINE=FAIL stage=configure" >&2
  tail -n 45 "$work/configure.out" "$work/configure.err" >&2 || true
  exit 1
fi
if ! (cd "$src" && make -j2 tcc >"$work/build.out" 2>"$work/build.err"); then
  echo "TINYCC_X86_64_BASELINE=FAIL stage=upstream-GCC-build" >&2
  tail -n 65 "$work/build.out" "$work/build.err" >&2 || true
  exit 1
fi
if ! "$src/tcc" -v >"$work/version.out" 2>"$work/version.err"; then
  echo "TINYCC_X86_64_BASELINE=FAIL stage=version" >&2
  cat "$work/version.out" "$work/version.err" >&2
  exit 1
fi
echo "TINYCC_X86_64_BASELINE=PASS compiler=GCC stage=configure,build,run"
# Unmodified tcc.c includes tcctools.c. ONE_SOURCE=0 is the upstream
# multi-TU build, using the configure-generated config.h.
if ! "$HOST_CC" -E -P -std=gnu11 -DTCC_TARGET_X86_64 -DONE_SOURCE=0 -I"$src" "$src/tcc.c" -o "$work/tcc.i" >"$work/cpp.out" 2>"$work/cpp.err"; then
  echo "TINYCC_X86_64_INPUT=FAIL stage=preprocess" >&2
  tail -n 35 "$work/cpp.err" >&2
  exit 1
fi
echo "TINYCC_X86_64_INPUT=PASS source=tcc.c lines=$(wc -l < "$work/tcc.i")"
if ! "$MINIC_CC" --target=x86_64-linux -S "$work/tcc.i" -o "$work/tcc.s" >"$work/minic.out" 2>"$work/minic.err"; then
  echo "TINYCC_X86_64_BLOCKER=MiniC source=tcc.c stage=compiler" >&2
  sed -n '1,35p' "$work/minic.err" >&2
  echo "TINYCC_X86_64_FRONTIER=BLOCKED first_tu=tcc.c" >&2
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
echo "TINYCC_X86_64_COMPILER=PASS source=tcc.c"
if ! "$HOST_CC" -c -x assembler "$work/tcc.s" -o "$work/tcc.o" >"$work/as.out" 2>"$work/as.err"; then
  echo "TINYCC_X86_64_BLOCKER=GNU_AS source=tcc.c" >&2
  sed -n '1,35p' "$work/as.err" >&2
  echo "TINYCC_X86_64_FRONTIER=BLOCKED first_tu=tcc.c" >&2
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
echo "TINYCC_X86_64_OBJECT=PASS source=tcc.c"
# The release's native x86-64 multi-TU build lists exactly nine
# additional C source files in its Makefile. No GCC-built TCC objects
# may be substituted into the MiniC output.
mkdir -p "$work/objects"
mv "$work/tcc.o" "$work/objects/tcc.o"
set -- "$work/objects/tcc.o"
done_count=1
block_count=0
for file in libtcc.c tccpp.c tccgen.c tccelf.c tccasm.c tccrun.c \
            x86_64-gen.c x86_64-link.c i386-asm.c; do
  stem=$(basename "$file" .c)
  if ! "$HOST_CC" -E -P -std=gnu11 -DTCC_TARGET_X86_64 -DONE_SOURCE=0 \
             -I"$src" "$src/$file" -o "$work/$stem.i" \
             >"$work/$stem.cpp.out" 2>"$work/$stem.cpp.err"; then
    echo "TINYCC_X86_64_BLOCKER=HOST_CPP source=$file" >&2
    sed -n '1,25p' "$work/$stem.cpp.err" >&2
    block_count=$((block_count + 1))
    continue
  fi
  if ! "$MINIC_CC" --target=x86_64-linux -S \
             "$work/$stem.i" -o "$work/$stem.s" \
             >"$work/$stem.minic.out" 2>"$work/$stem.minic.err"; then
    echo "TINYCC_X86_64_BLOCKER=MiniC source=$file stage=compiler" >&2
    sed -n '1,18p' "$work/$stem.minic.err" >&2
    block_count=$((block_count + 1))
    continue
  fi
  if ! "$HOST_CC" -c -x assembler "$work/$stem.s" \
             -o "$work/objects/$stem.o" \
             >"$work/$stem.as.out" 2>"$work/$stem.as.err"; then
    echo "TINYCC_X86_64_BLOCKER=GNU_AS source=$file stage=assembler" >&2
    sed -n '1,18p' "$work/$stem.as.err" >&2
    block_count=$((block_count + 1))
    continue
  fi
  done_count=$((done_count + 1))
  set -- "$@" "$work/objects/$stem.o"
  echo "TINYCC_X86_64_TU=PASS source=$file completed=$done_count"
done
if test "$block_count" -ne 0; then
  echo "TINYCC_X86_64_FRONTIER=BLOCKED objects=$done_count total=10 blocked=$block_count"
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
echo "TINYCC_X86_64_OBJECTS=PASS count=$done_count"
if ! "$HOST_CC" -no-pie "$@" -ldl -lm -o "$work/tcc-minic" \
       >"$work/link.out" 2>"$work/link.err"; then
  echo "TINYCC_X86_64_BLOCKER=GNU_LINK objects=$done_count" >&2
  sed -n '1,35p' "$work/link.err" >&2
  echo "TINYCC_X86_64_FRONTIER=BLOCKED stage=link"
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
echo "TINYCC_X86_64_LINK=PASS objects=$done_count GNU_CRT=1"
if ! "$work/tcc-minic" -v >"$work/minic.version.out" 2>"$work/minic.version.err"; then
  echo "TINYCC_X86_64_FRONTIER=BLOCKED stage=native-version-run"
  sed -n '1,30p' "$work/minic.version.err" >&2
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
if ! cmp -s "$work/version.out" "$work/minic.version.out" ||
   ! cmp -s "$work/version.err" "$work/minic.version.err"; then
  echo "TINYCC_X86_64_FRONTIER=BLOCKED stage=version-output-mismatch"
  diff -u "$work/version.out" "$work/minic.version.out" >&2 || true
  diff -u "$work/version.err" "$work/minic.version.err" >&2 || true
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
echo "TINYCC_X86_64_RUNTIME=PASS executable=tcc version_oracle=GCC"

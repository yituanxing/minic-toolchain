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
echo "TINYCC_X86_64_FRONTIER=FIRST_TU_ONLY full_tcc=NOT_TESTED"

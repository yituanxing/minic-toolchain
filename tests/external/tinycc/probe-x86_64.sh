#!/bin/sh
# Pristine TinyCC 0.9.27: all 10 TUs plus native compiler/consumer C runtime.
# This is a pinned scoped gate, not a claim of upstream full-suite or self-hosting.
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
# The original upstream Makefile passes configured CONFIG_TRIPLET into
# EVERY C translation unit. A former ad hoc MiniC preprocessor command
# omitted it, thereby losing /usr/include/<triplet> in the compiled
# TinyCC system search path. Preserve the configure-generated setting,
# not a forced include path or a patch to libtcc.c.
# TinyCC 0.9.27's configure writes these via print_mak as
# NATIVE_DEFINES+=-DCONFIG_TRIPLET="\\"x86_64-linux-gnu\\""
# (not CONFIG_TRIPLET=...). Read its actual Makefile macro.
triplet=$(python3 - "$src/config.mak" <<'PY'
import re, sys
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("NATIVE_DEFINES") and "CONFIG_TRIPLET=" in line:
        m = re.search(r"CONFIG_TRIPLET=.*?([A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)+)", line)
        if m:
            print(m.group(1))
            break
PY
)
if test -z "$triplet" || ! test -d "/usr/include/$triplet"; then
  echo "TINYCC_X86_64_CONFIG=FAIL missing-upstream-triplet triplet=$triplet" >&2
  exit 1
fi
config_triplet_flag="-DCONFIG_TRIPLET=\"$triplet\""
echo "TINYCC_X86_64_CONFIG=PASS source=upstream-config.mak triplet=$triplet"

# Unmodified tcc.c includes tcctools.c. ONE_SOURCE=0 is the upstream
# multi-TU build, using the configure-generated config.h.
if ! "$HOST_CC" -E -P -std=gnu11 -DTCC_TARGET_X86_64 -DONE_SOURCE=0 "$config_triplet_flag" -I"$src" "$src/tcc.c" -o "$work/tcc.i" >"$work/cpp.out" 2>"$work/cpp.err"; then
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
             "$config_triplet_flag" -I"$src" "$src/$file" -o "$work/$stem.i" \
             >"$work/$stem.cpp.out" 2>"$work/$stem.cpp.err"; then
    echo "TINYCC_X86_64_BLOCKER=HOST_CPP source=$file" >&2
    sed -n '1,25p' "$work/$stem.cpp.err" >&2
    block_count=$((block_count + 1))
    continue
  fi
  if ! MINIC_X86_CALL_TRACE=1 CORE_FAST_TRACE=1 "$MINIC_CC" --target=x86_64-linux -S \
             "$work/$stem.i" -o "$work/$stem.s" \
             >"$work/$stem.minic.out" 2>"$work/$stem.minic.err"; then
    echo "TINYCC_X86_64_BLOCKER=MiniC source=$file stage=compiler" >&2
    grep -E 'X86_CALL_GATE|X86_CALL_ARG|FRONTEND_LOOKUP_DETAIL' "$work/$stem.minic.err" >&2 || true
    sed -n '1,18p' "$work/$stem.minic.err" >&2
    python3 - "$work/$stem.i" "$work/$stem.minic.err" <<'PY' >&2
import re
import sys
text = open(sys.argv[2], encoding="utf-8", errors="replace").read()
match = re.search(r"\.i:(\d+):(\d+): error:", text)
if match:
    line, col = int(match[1]), int(match[2])
    with open(sys.argv[1], encoding="utf-8", errors="replace") as stream:
        source = next((x for i, x in enumerate(stream, 1) if i == line), "")
    print("TINYCC_X86_64_DIAGNOSTIC_CONTEXT line=%d col=%d fragment=%r" %
          (line, col, source[max(0, col-151):col+149]))
PY
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

# Functional round-trip: the MiniC-produced TinyCC must compile ORIGINAL
# new C input into a real x86_64 ELF relocatable object. The host GNU
# compiler only supplies the CRT/linker and independent reference build;
# it must never substitute for a TinyCC-generated consumer object.
cat > "$work/consumer.c" <<'EOF'
int minic_tinycc_compute(void) {
    int i, total = 0;
    for (i = 0; i < 16; ++i)
        total += (i & 1) ? 3 : 2;
    return total + 2;
}
EOF
cat > "$work/consumer_main.c" <<'EOF'
extern int minic_tinycc_compute(void);
int main(void) { return minic_tinycc_compute(); }
EOF
if ! "$work/tcc-minic" -c "$work/consumer.c" -o "$work/consumer.minic-tcc.o" \
      >"$work/consumer.out" 2>"$work/consumer.err"; then
  echo "TINYCC_X86_64_BLOCKER=COMPILED_TCC stage=compile-consumer" >&2
  tail -n 35 "$work/consumer.out" "$work/consumer.err" >&2 || true
  echo "TINYCC_X86_64_FRONTIER=BLOCKED stage=compile-new-C"
  if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
  exit 1
fi
if ! "$src/tcc" -c "$work/consumer.c" -o "$work/consumer.gcc-tcc.o" \
      >"$work/gcc-tcc-consumer.out" 2>"$work/gcc-tcc-consumer.err" ||
   ! "$HOST_CC" -c "$work/consumer.c" -o "$work/consumer.gnu.o" ||
   ! "$HOST_CC" -c "$work/consumer_main.c" -o "$work/consumer_main.o"; then
  echo "TINYCC_X86_64_BASELINE=FAIL stage=consumer-oracles" >&2
  tail -n 30 "$work/gcc-tcc-consumer.err" >&2 || true
  exit 1
fi
for candidate in minic-tcc gcc-tcc gnu; do
  if ! "$HOST_CC" -no-pie "$work/consumer_main.o" \
       "$work/consumer.$candidate.o" -o "$work/consumer.$candidate.bin" \
       >"$work/link.$candidate.out" 2>"$work/link.$candidate.err"; then
    echo "TINYCC_X86_64_BLOCKER=CONSUMER_LINK backend=$candidate" >&2
    tail -n 35 "$work/link.$candidate.err" >&2 || true
    if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
    exit 1
  fi
  set +e
  "$work/consumer.$candidate.bin"
  rc=$?
  set -e
  if test "$rc" -ne 42; then
    echo "TINYCC_X86_64_BLOCKER=CONSUMER_RUNTIME backend=$candidate result=$rc expected=42" >&2
    if test "${TINYCC_X86_64_DISCOVERY:-0}" = 1; then exit 0; fi
    exit 1
  fi
  echo "TINYCC_X86_64_CONSUMER=PASS backend=$candidate exit=42"
done
echo "TINYCC_X86_64_FUNCTIONAL=PASS original_tcc=MiniC consumer_c=TinyCC consumer_o=MiniC-built-TinyCC linker=GNU_CC"



# Extra unmodified upstream tests2 programs. The MiniC-built TinyCC
# must compile new C files to .o, linked by GNU, and byte-exactly agree
# with an independent GCC-built TinyCC executable.
for case_name in 00_assignment 03_struct 04_for 05_array 07_function \
                 10_pointer 15_recursion 22_floating_point 25_quicksort 30_hanoi; do
    input="$src/tests/tests2/$case_name.c"
    if ! "$src/tcc" -B"$src" -I"$src/include" -I"$src" -c "$input" -o "$work/tests2.$case_name.ref.o" \
         >"$work/tests2.$case_name.ref.log" 2>&1 ||
       ! "$HOST_CC" -no-pie "$work/tests2.$case_name.ref.o" \
         -o "$work/tests2.$case_name.ref.bin" \
         >>"$work/tests2.$case_name.ref.log" 2>&1; then
        echo "TINYCC_X86_64_TESTS2_BASELINE=FAIL case=$case_name" >&2
        tail -n 25 "$work/tests2.$case_name.ref.log" >&2
        exit 1
    fi
    set +e
    "$work/tests2.$case_name.ref.bin" >"$work/tests2.$case_name.ref.out" \
        2>"$work/tests2.$case_name.ref.err"
    ref_rc=$?
    set -e
    if ! "$work/tcc-minic" -B"$src" -I"$src/include" -I"$src" -c "$input" -o "$work/tests2.$case_name.mini.o" \
         >"$work/tests2.$case_name.mini.log" 2>&1 ||
       ! "$HOST_CC" -no-pie "$work/tests2.$case_name.mini.o" \
         -o "$work/tests2.$case_name.mini.bin" \
         >>"$work/tests2.$case_name.mini.log" 2>&1; then
        echo "TINYCC_X86_64_TESTS2=BLOCKED case=$case_name stage=compile-or-link" >&2
        tail -n 30 "$work/tests2.$case_name.mini.log" >&2
        # Compare the ORIGINAL compiler's effective system include paths
        # with the MiniC-built compiler. Do not add a forced -I for
        # /usr/include/x86_64-linux-gnu: it would mask the real difference.
        "$src/tcc" -vv -B"$src" -I"$src/include" -I"$src"            -c "$input" -o "$work/diagnose.ref.o"            >"$work/tcc.vv.ref" 2>&1 || true
        "$work/tcc-minic" -vv -B"$src" -I"$src/include" -I"$src"            -c "$input" -o "$work/diagnose.mini.o"            >"$work/tcc.vv.mini" 2>&1 || true
        echo "TINYCC_X86_64_DIAG=original-GCC-compiler-search" >&2
        tail -n 45 "$work/tcc.vv.ref" >&2
        echo "TINYCC_X86_64_DIAG=MiniC-compiler-search" >&2
        tail -n 45 "$work/tcc.vv.mini" >&2
        if test "$(printenv TINYCC_X86_64_DISCOVERY || true)" = 1; then exit 0; fi
        exit 1
    fi
    set +e
    "$work/tests2.$case_name.mini.bin" >"$work/tests2.$case_name.mini.out" \
        2>"$work/tests2.$case_name.mini.err"
    mini_rc=$?
    set -e
    if test "$ref_rc" -ne "$mini_rc" ||
       ! cmp -s "$work/tests2.$case_name.ref.out" "$work/tests2.$case_name.mini.out" ||
       ! cmp -s "$work/tests2.$case_name.ref.err" "$work/tests2.$case_name.mini.err"; then
        echo "TINYCC_X86_64_TESTS2=BLOCKED case=$case_name stage=runtime ref_rc=$ref_rc mini_rc=$mini_rc" >&2
        diff -u "$work/tests2.$case_name.ref.out" "$work/tests2.$case_name.mini.out" >&2 || true
        if test "$(printenv TINYCC_X86_64_DISCOVERY || true)" = 1; then exit 0; fi
        exit 1
    fi
    echo "TINYCC_X86_64_TESTS2=PASS case=$case_name exit=$mini_rc oracle=GCC-built-TinyCC byte-exact"
done
echo "TINYCC_X86_64_TESTS2_SUITE=PASS cases=10 original_upstream=1"

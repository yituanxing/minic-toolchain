#!/usr/bin/env bash
# Original zlib 1.3.1, all 15 C translation units plus an independent real
# caller. GCC emits .s exactly once per TU; either GNU AS or MiniAS assembles
# those identical .s files; both binaries use GNU BFD, GNU CRT and glibc.
# Acceptance is real QEMU execution of compression/decompression and gzip.
# Object/ELF binary identity is not an acceptance condition.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-zlib-runtime.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-gcc-zlib-runtime/$arch"}
upstream_commit=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
mkdir -p "$work/asm" "$work/gnu" "$work/mini" "$work/evidence"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; gas=${GNU_AS:-as}; qemu=${QEMU_USER:-qemu-x86_64}
    gcc_flags=(); gas_flags=(--64)
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}
    gas=${GNU_AS:-aarch64-linux-gnu-as}; qemu=${QEMU_USER:-qemu-aarch64}
    gcc_flags=(); gas_flags=(-march=armv8-a)
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}
    gas=${GNU_AS:-riscv64-linux-gnu-as}; qemu=${QEMU_USER:-qemu-riscv64}
    gcc_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    gas_flags=(-march=rv64gc -mabi=lp64d -mno-relax)
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}
    gas=${GNU_AS:-loongarch64-linux-gnu-as}
    qemu=${QEMU_USER:-qemu-loongarch64}
    gcc_flags=(-mabi=lp64d); gas_flags=(-mabi=lp64d)
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    ;;
  *) echo "MINIAS_ZLIB_RUNTIME=ERROR invalid_arch=$arch" >&2; exit 64 ;;
esac

for tool in "$cc" "$gas" "$qemu" "$minias" git sha256sum tar curl timeout cmp; do
  if ! command -v "$tool" >/dev/null; then
    echo "MINIAS_ZLIB_RUNTIME=ERROR arch=$arch missing_tool=$tool" >&2
    exit 127
  fi
done

"$cc" --version >"$work/evidence/gcc-version.txt"
"$gas" --version >"$work/evidence/gnu-as-version.txt"
"$minias" --version >"$work/evidence/minias-version.txt" 2>&1 || true
sha256sum "$minias" >"$work/evidence/minias.sha256"
printf 'head=%s\narch=%s\nqemu=%s\nsource_commit=%s\n' \
  "${GITHUB_SHA:-local-uncommitted}" "$arch" "$qemu" "$upstream_commit" \
  >"$work/evidence/identity.txt"

if [[ -n "${ZLIB_SOURCE_DIR:-}" ]]; then
  src=$ZLIB_SOURCE_DIR
  source_mode=preexisting
else
  src="$work/upstream"
  source_mode=github-pinned
  if [[ ! -f "$src/zlib.h" ]]; then
    mkdir -p "$src"
    if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
      "https://github.com/madler/zlib/archive/$upstream_commit.tar.gz" \
      -o "$work/zlib-pinned.tar.gz" \
      >"$work/evidence/zlib-download.stdout" \
      2>"$work/evidence/zlib-download.stderr"; then
      echo "MINIAS_ZLIB_RUNTIME=FAIL arch=$arch stage=download" >&2
      exit 1
    fi
    if ! tar -xzf "$work/zlib-pinned.tar.gz" -C "$src" --strip-components=1 \
      >"$work/evidence/zlib-extract.stdout" \
      2>"$work/evidence/zlib-extract.stderr"; then
      echo "MINIAS_ZLIB_RUNTIME=FAIL arch=$arch stage=extract" >&2
      exit 1
    fi
  fi
fi

# Independently pinned upstream Git blob identities, established by the
# existing tests/external/zlib original-source oracle. No generated substitutes.
source_ids=(
  'zlib.h 8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0'
  'deflate.c 012ea8148e8d21cf515a9ff47d4af719d049b276'
  'zconf.h 62adc8d8431f2f9149ae0b1583915e21a28dd8b5'
  'test/example.c c3521dd5934f716bc7c33fe1c5fd4a361c985e76'
)
for identity in "${source_ids[@]}"; do
  read -r relative expected <<<"$identity"
  if [[ ! -f "$src/$relative" ]]; then
    echo "MINIAS_ZLIB_RUNTIME=FAIL arch=$arch stage=missing-upstream-file file=$relative" >&2
    exit 1
  fi
  actual=$(git hash-object "$src/$relative")
  if [[ "$actual" != "$expected" ]]; then
    echo "MINIAS_ZLIB_RUNTIME=FAIL arch=$arch stage=upstream-pin file=$relative actual=$actual expected=$expected" >&2
    exit 1
  fi
done
printf 'zlib_upstream=%s\nsource_mode=%s\n' "$upstream_commit" "$source_mode" \
  >"$work/evidence/source-provenance.txt"

# Keep the complete original library: do not cherry-pick an easier subset.
units=(adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
       infback inffast inflate inftrees trees uncompr zutil runtime-gcc-zlib)
failures=()
gnu_assembled=0
mini_assembled=0
# CI must surface the precise first GNU/MiniAS diagnostic for every original
# translation unit, rather than exposing only a count. Complete logs remain
# in the per-architecture artifact for later full failure analysis.
emit_diagnostic() {
  local stage=$1 unit=$2
  local log="$work/evidence/$stage-$unit.log"
  printf 'MINIAS_ZLIB_DIAGNOSTIC arch=%s stage=%s unit=%s file=%s\n' \
    "$arch" "$stage" "$unit" "$log" >&2
  if [[ -f "$log" ]]; then
    sed -n '1,20p' "$log" >&2
  else
    printf 'MINIAS_ZLIB_DIAGNOSTIC arch=%s missing_log=%s\n' \
      "$arch" "$log" >&2
  fi
}
: >"$work/evidence/source-and-assembly.sha256"
for unit in "${units[@]}"; do
  if [[ "$unit" == runtime-gcc-zlib ]]; then
    source="$repo/tests/assembler/runtime-gcc-zlib.c"
  else
    source="$src/$unit.c"
  fi
  asm="$work/asm/$unit.s"
  rm -f "$asm" "$work/gnu/$unit.o" "$work/mini/$unit.o"
  if ! "$cc" "${gcc_flags[@]}" -O1 -g0 -std=c11 -fno-pie -fno-pic \
      -I"$src" -S "$source" -o "$asm" \
      >"$work/evidence/gcc-$unit.log" 2>&1 || [[ ! -s "$asm" ]]; then
    failures+=("gcc-S:$unit")
    echo "MINIAS_ZLIB_TU=FAIL arch=$arch unit=$unit stage=gcc-S" >&2
    emit_diagnostic gcc "$unit"
    continue
  fi
  sha256sum "$source" "$asm" >>"$work/evidence/source-and-assembly.sha256"
  if "$gas" "${gas_flags[@]}" -o "$work/gnu/$unit.o" "$asm" \
      >"$work/evidence/gnu-as-$unit.log" 2>&1; then
    ((gnu_assembled += 1))
  else
    failures+=("gnu-as:$unit")
    echo "MINIAS_ZLIB_TU=FAIL arch=$arch unit=$unit stage=gnu-as" >&2
    emit_diagnostic gnu-as "$unit"
  fi
  if "$minias" "${mini_flags[@]}" -o "$work/mini/$unit.o" "$asm" \
      >"$work/evidence/mini-as-$unit.log" 2>&1; then
    ((mini_assembled += 1))
  else
    failures+=("mini-as:$unit")
    echo "MINIAS_ZLIB_TU=FAIL arch=$arch unit=$unit stage=mini-as" >&2
    emit_diagnostic mini-as "$unit"
  fi
done

printf 'MINIAS_ZLIB_ASSEMBLY arch=%s total=%d gnu=%d mini=%d\n' \
  "$arch" "${#units[@]}" "$gnu_assembled" "$mini_assembled"

for flavor in gnu mini; do
  flavor_dir="$work/$flavor"
  rm -f "$flavor_dir/program" "$work/evidence/$flavor-stdout.txt" \
    "$work/evidence/$flavor-stderr.txt" "$work/evidence/$flavor-exit.txt"
  objects=()
  for unit in "${units[@]}"; do
    [[ -s "$flavor_dir/$unit.o" ]] && objects+=("$flavor_dir/$unit.o")
  done
  if (( ${#objects[@]} != ${#units[@]} )); then
    failures+=("missing-objects:$flavor:${#objects[@]}/${#units[@]}")
    echo "MINIAS_ZLIB_LINK=BLOCKED arch=$arch flavor=$flavor objects=${#objects[@]}/${#units[@]}" >&2
    continue
  fi
  if ! "$cc" "${gcc_flags[@]}" -static -no-pie -fuse-ld=bfd \
      -Wl,--fatal-warnings -Wl,-v -o "$flavor_dir/program" "${objects[@]}" \
      >"$work/evidence/$flavor-link.log" 2>&1 || \
      [[ ! -s "$flavor_dir/program" ]]; then
    failures+=("gnu-bfd-link:$flavor")
    echo "MINIAS_ZLIB_LINK=FAIL arch=$arch flavor=$flavor" >&2
    printf 'MINIAS_ZLIB_DIAGNOSTIC arch=%s stage=gnu-bfd-link flavor=%s\n' \
      "$arch" "$flavor" >&2
    sed -n '1,20p' "$work/evidence/$flavor-link.log" >&2
    continue
  fi
  if ! grep -Fq 'GNU ld' "$work/evidence/$flavor-link.log"; then
    failures+=("gnu-bfd-provenance:$flavor")
    echo "MINIAS_ZLIB_LINK=FAIL arch=$arch flavor=$flavor reason=non-gnu-bfd" >&2
    continue
  fi
  result=0
  ( cd "$flavor_dir" && timeout --signal=TERM 60s "$qemu" "$flavor_dir/program" ) \
    >"$work/evidence/$flavor-stdout.txt" \
    2>"$work/evidence/$flavor-stderr.txt" || result=$?
  printf '%s\n' "$result" >"$work/evidence/$flavor-exit.txt"
  if (( result != 0 )) || [[ -s "$work/evidence/$flavor-stderr.txt" ]] || \
     ! grep -Eq '^ZLIB_RUNTIME=PASS bytes=32768 crc=[[:xdigit:]]{8} adler=[[:xdigit:]]{8} gzip=PASS$' \
       "$work/evidence/$flavor-stdout.txt"; then
    failures+=("runtime:$flavor:exit=$result")
    echo "MINIAS_ZLIB_RUN=FAIL arch=$arch flavor=$flavor exit=$result" >&2
  else
    echo "MINIAS_ZLIB_RUN=PASS arch=$arch flavor=$flavor linked=gnu-bfd qemu=$qemu"
  fi
done

if [[ -f "$work/evidence/gnu-exit.txt" && -f "$work/evidence/mini-exit.txt" ]]; then
  for channel in stdout stderr exit; do
    if ! cmp -s "$work/evidence/gnu-$channel.txt" \
                 "$work/evidence/mini-$channel.txt"; then
      failures+=("runtime-difference:$channel")
      echo "MINIAS_ZLIB_DIFF=FAIL arch=$arch channel=$channel" >&2
    fi
  done
else
  failures+=(missing-runtime-side)
fi

printf '%s\n' "${failures[@]}" >"$work/evidence/failures.txt"
if (( ${#failures[@]} != 0 )); then
  printf 'MINIAS_ZLIB_RUNTIME=FAIL arch=%s tus=%d mini_assembled=%d reasons=%s\n' \
    "$arch" "${#units[@]}" "$mini_assembled" "${failures[*]}" | \
    tee "$work/evidence/summary.txt" >&2
  exit 1
fi
printf 'MINIAS_ZLIB_RUNTIME=PASS arch=%s tus=%d gnu_bfd=2 qemu_runs=2 gzip=PASS\n' \
  "$arch" "${#units[@]}" | tee "$work/evidence/summary.txt"

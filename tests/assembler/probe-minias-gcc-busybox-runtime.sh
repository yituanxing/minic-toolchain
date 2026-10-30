#!/usr/bin/env bash
# BusyBox 1.36.1 original Kconfig/Kbuild -> GCC .s -> MiniAS for EVERY
# target object -> GNU Kbuild archive/link -> real BusyBox applet execution.
# The GNU side starts from exactly the same generated .s files. There is no
# GNU assembler fallback when a required MiniAS target object fails.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
arch=${1:?usage: probe-minias-gcc-busybox-runtime.sh x86_64|aarch64|riscv64|loongarch64}
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$repo/build/minias-busybox-runtime/$arch"}
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
mkdir -p "$work/evidence" "$work/gnu" "$work/mini" "$work/mini-as-logs"

case "$arch" in
  x86_64)
    cc=${CC:-gcc}; prefix=''; qemu=${QEMU_USER:-qemu-x86_64}
    mini_flags=(--target=x86_64 -march=x86-64 -mabi=sysv)
    ;;
  aarch64)
    cc=${CC:-aarch64-linux-gnu-gcc}; prefix=aarch64-linux-gnu-
    qemu=${QEMU_USER:-qemu-aarch64}
    mini_flags=(--target=aarch64 -march=armv8-a -mabi=lp64)
    ;;
  riscv64)
    cc=${CC:-riscv64-linux-gnu-gcc}; prefix=riscv64-linux-gnu-
    qemu=${QEMU_USER:-qemu-riscv64}
    mini_flags=(--target=riscv -march=rv64gc -mabi=lp64d)
    ;;
  loongarch64)
    cc=${CC:-loongarch64-linux-gnu-gcc-13}; prefix=loongarch64-linux-gnu-
    qemu=${QEMU_USER:-qemu-loongarch64}
    mini_flags=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
    ;;
  *) echo "MINIAS_BUSYBOX_RUNTIME=ERROR unsupported_arch=$arch" >&2; exit 64 ;;
esac

for tool in "$cc" "$qemu" "$minias" curl tar git make sed sha256sum cmp timeout find; do
  if ! command -v "$tool" >/dev/null; then
    echo "MINIAS_BUSYBOX_RUNTIME=ERROR missing_tool=$tool arch=$arch" >&2
    exit 127
  fi
done

printf 'head=%s\narch=%s\ncompiler=%s\nrunner=%s\n' \
  "${GITHUB_SHA:-local-uncommitted}" "$arch" "$cc" "$qemu" \
  >"$work/evidence/identity.txt"
"$cc" --version >"$work/evidence/gcc-version.txt"
sha256sum "$minias" >"$work/evidence/minias.sha256"

# Tar archive is immutable for this run; original upstream .c, .h, Kbuild and
# Kconfig are not patched. Our only configuration changes are through .config.
archive=${BUSYBOX_ARCHIVE:-"$work/busybox-$commit.tar.gz"}
if [[ -z "${BUSYBOX_ARCHIVE:-}" ]]; then
  if ! timeout 150s curl -fLSs --retry 2 \
      "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
      -o "$archive" >"$work/evidence/source-fetch.log" 2>&1; then
    echo 'MINIAS_BUSYBOX_RUNTIME=FAIL stage=source-download' >&2
    exit 1
  fi
fi
sha256sum "$archive" >"$work/evidence/source.sha256"
src="$work/source"
if [[ -e "$src/Makefile" ]]; then
  echo "MINIAS_BUSYBOX_RUNTIME=ERROR work_dir_not_fresh=$work" >&2
  exit 64
fi
mkdir -p "$src"
if ! tar -xzf "$archive" --strip-components=1 -C "$src" \
     >"$work/evidence/source-extract.log" 2>&1 ||
   ! sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" "$src" \
     >"$work/evidence/source-attestation.log" 2>&1; then
  echo 'MINIAS_BUSYBOX_RUNTIME=FAIL stage=pinned-source-identity' >&2
  exit 1
fi

# Original upstream Kbuild, 10 selected real applets including ash; all the
# selected applets' actual C owners AND all enabled shared libbb C owners are
# assembled by MiniAS, not just an artificially small applet source subset.
if ! make -C "$src" -j2 HOSTCC=cc CC="$cc" CROSS_COMPILE="$prefix" \
    allnoconfig >"$work/evidence/config.log" 2>&1; then
  echo 'MINIAS_BUSYBOX_RUNTIME=FAIL stage=allnoconfig' >&2
  exit 1
fi
for config in STATIC CAT ECHO HEAD WC GREP CUT SORT UNIQ TR ASH; do
  if ! grep -q "^# CONFIG_${config} is not set$" "$src/.config"; then
    echo "MINIAS_BUSYBOX_RUNTIME=FAIL stage=config-missing key=$config" >&2
    exit 1
  fi
  sed -i "s/^# CONFIG_${config} is not set$/CONFIG_${config}=y/" "$src/.config"
done
sed -i 's/^CONFIG_EXTRA_CFLAGS=""$/CONFIG_EXTRA_CFLAGS="-save-temps=obj -g0"/' "$src/.config"
if ! make -C "$src" HOSTCC=cc CC="$cc" CROSS_COMPILE="$prefix" \
    oldconfig >"$work/evidence/oldconfig.log" 2>&1 ||
   ! make -C "$src" -j2 HOSTCC=cc CC="$cc" CROSS_COMPILE="$prefix" \
    busybox >"$work/evidence/gnu-kbuild.log" 2>&1 ||
   ! test -s "$src/busybox"; then
  echo 'MINIAS_BUSYBOX_RUNTIME=FAIL stage=GNU-original-Kbuild' >&2
  exit 1
fi
cp "$src/.config" "$work/evidence/busybox.config"
cp "$src/busybox" "$work/gnu/busybox"
sha256sum "$work/gnu/busybox" >"$work/evidence/gnu-program.sha256"
if ! timeout --signal=TERM 30s "$qemu" "$work/gnu/busybox" \
    echo busybox-runtime-pass >"$work/evidence/gnu-preflight.stdout" \
    2>"$work/evidence/gnu-preflight.stderr" ||
   ! grep -qx 'busybox-runtime-pass' "$work/evidence/gnu-preflight.stdout" ||
   test -s "$work/evidence/gnu-preflight.stderr"; then
  echo "MINIAS_BUSYBOX_RUNTIME=FAIL arch=$arch stage=GNU-QEMU-preflight" >&2
  exit 1
fi
echo "MINIAS_BUSYBOX_GNU_RUNTIME=PASS arch=$arch source_pinned=1 GNU_Kbuild=1 runner=$qemu"

# GCC -save-temps=obj provides the exact immutable assembler input for each
# original Kbuild C owner, so GNU and MiniAS never receive different sources.
# No GNU object may be silently retained as a required MiniAS leaf owner.
leaf_count=0
fail_count=0
probe_count=0
while IFS= read -r -d '' asm; do
  relative=${asm#"$src"/}
  stem=${relative%.s}
  gnu_object="$src/$stem.o"
  mini_object="$work/mini/$stem.o"
  mini_log="$work/mini-as-logs/$stem.log"
  mkdir -p "$(dirname "$mini_object")" "$(dirname "$mini_log")"
  if ! test -s "$gnu_object"; then
    # GCC's configure/link capability probes also produce tmp.*.s, but no
    # Kbuild object. They are not production leaves and are never linked.
    case "$relative" in
      tmp.*.s) ((probe_count += 1)); continue ;;
      *)
        echo "MINIAS_BUSYBOX_OBJECT=FAIL target=$arch owner=$relative reason=missing-gnu-object" >&2
        ((fail_count += 1))
        continue
        ;;
    esac
  fi
  ((leaf_count += 1))
  if [[ "$relative" == scripts/kconfig/* ]]; then
    # The scripts/kconfig executables are built with HOSTCC, not with the
    # target compiler; reject unexpected target-assembly ownership leakage.
    echo "MINIAS_BUSYBOX_RUNTIME=FAIL stage=host-tool-target-assembly source=$relative" >&2
    exit 1
  fi
  if ! "$minias" "${mini_flags[@]}" -o "$mini_object" "$asm" \
      >"$mini_log" 2>&1 || ! test -s "$mini_object"; then
    mkdir -p "$work/evidence/failing-asm/$(dirname "$relative")"
    cp "$asm" "$work/evidence/failing-asm/$relative"
    echo "MINIAS_BUSYBOX_OBJECT=FAIL target=$arch owner=$relative reason=mini-as" >&2
    ((fail_count += 1))
  fi
done < <(find "$src" -type f -name '*.s' -print0 | sort -z)

printf 'C_owners=%d\nMiniAS_failures=%d\nDiscarded_GCC_capability_probe_asm=%d\n' \
  "$leaf_count" "$fail_count" "$probe_count" \
  >"$work/evidence/coverage.txt"
if (( leaf_count < 60 || fail_count != 0 )); then
  echo "MINIAS_BUSYBOX_RUNTIME=FAIL arch=$arch stage=all-C-owners MiniAS=$((leaf_count-fail_count))/$leaf_count" >&2
  exit 1
fi

# Save the original GNU C objects as provenance; replace only those objects
# for which MiniAS succeeded, then let ORIGINAL Kbuild use GNU ld and ar.
while IFS= read -r -d '' asm; do
  relative=${asm#"$src"/}
  stem=${relative%.s}
  gnu_object="$src/$stem.o"
  [[ -s "$gnu_object" ]] || continue
  mini_object="$work/mini/$stem.o"
  saved_gnu="$work/gnu/objects/$stem.o"
  mkdir -p "$(dirname "$saved_gnu")"
  cp "$gnu_object" "$saved_gnu"
  cp "$mini_object" "$gnu_object"
done < <(find "$src" -type f -name '*.s' -print0 | sort -z)

# All *_built-in.o are GNU ld -r aggregates, not GCC C leaf owners. Rebuild
# them and all GNU ar archives using the MiniAS leaves; never rerun GCC -c.
while IFS= read -r -d '' aggregate; do
  rm -f "$aggregate"
done < <(find "$src" -type f -name 'built-in.o' -print0)
while IFS= read -r -d '' archive_file; do
  rm -f "$archive_file"
done < <(find "$src" -type f -name 'lib.a' -print0)
rm -f "$src/busybox" "$src/busybox_unstripped"
if ! make -C "$src" -j2 HOSTCC=cc CC="$cc" CROSS_COMPILE="$prefix" \
    busybox >"$work/evidence/mini-relink.log" 2>&1 ||
   ! test -s "$src/busybox"; then
  echo "MINIAS_BUSYBOX_RUNTIME=FAIL arch=$arch stage=GNU-rearchive-or-link" >&2
  exit 1
fi

# Verify GNU Kbuild did not secretly regenerate a MiniAS-produced C owner
# with GCC and GNU AS while re-linking.
replaced=0
while IFS= read -r -d '' asm; do
  relative=${asm#"$src"/}
  stem=${relative%.s}
  [[ -s "$work/mini/$stem.o" ]] || continue
  if ! cmp -s "$work/mini/$stem.o" "$src/$stem.o"; then
    echo "MINIAS_BUSYBOX_RUNTIME=FAIL arch=$arch stage=GNU-overwrote-owner owner=$relative" >&2
    exit 1
  fi
  ((replaced += 1))
done < <(find "$src" -type f -name '*.s' -print0 | sort -z)
test "$replaced" -eq "$leaf_count"
cp "$src/busybox" "$work/mini/busybox"
sha256sum "$work/mini/busybox" >"$work/evidence/mini-program.sha256"

# Every applet is a separate real QEMU/native execution. Use byte-for-byte
# stdout/stderr and exit code, with the same pinned upstream program on the
# GNU side. No strict ELF parity is used as a correctness claim.
printf 'one\nsecond\n' >"$work/evidence/text"
printf 'a:3\nb:1\nb:1\nc:2\n' >"$work/evidence/columns"
printf 'delta\nalpha\nbeta\n' >"$work/evidence/words"
cases=(echo cat head wc grep cut sort uniq tr ash)
run_case() {
  local flavor=$1 case_name=$2 binary=$3 result=0
  local stdout="$work/evidence/$flavor-$case_name.stdout"
  local stderr="$work/evidence/$flavor-$case_name.stderr"
  local -a args=()
  local stdin_file=/dev/null
  case "$case_name" in
    echo) args=(echo busybox-runtime-pass) ;;
    cat) args=(cat "$work/evidence/text") ;;
    head) args=(head -n 1 "$work/evidence/text") ;;
    wc) args=(wc -l "$work/evidence/text") ;;
    grep) args=(grep -F second "$work/evidence/text") ;;
    cut) args=(cut -d: -f2 "$work/evidence/columns") ;;
    sort) args=(sort "$work/evidence/words") ;;
    uniq)
      args=(uniq)
      printf 'alpha\nbeta\nbeta\n' >"$work/evidence/uniq.stdin"
      stdin_file="$work/evidence/uniq.stdin" ;;
    tr)
      args=(tr a-z A-Z)
      printf 'abc-xyz\n' >"$work/evidence/tr.stdin"
      stdin_file="$work/evidence/tr.stdin" ;;
    ash) args=(ash -c 'value=7; test "$value" -eq 7 && echo ash-runtime-pass') ;;
  esac
  timeout --signal=TERM 30s "$qemu" "$binary" "${args[@]}" \
    <"$stdin_file" >"$stdout" 2>"$stderr" || result=$?
  printf '%s\n' "$result" >"$work/evidence/$flavor-$case_name.exit"
  if (( result != 0 )); then
    echo "MINIAS_BUSYBOX_APPLET=FAIL arch=$arch flavor=$flavor name=$case_name exit=$result" >&2
    return 1
  fi
}

applet_failures=0
for case_name in "${cases[@]}"; do
  case_errors=0
  if ! run_case gnu "$case_name" "$work/gnu/busybox"; then
    ((case_errors += 1))
    ((applet_failures += 1))
  fi
  if ! run_case mini "$case_name" "$work/mini/busybox"; then
    ((case_errors += 1))
    ((applet_failures += 1))
  fi
  for channel in exit stdout stderr; do
    if ! cmp -s "$work/evidence/gnu-$case_name.$channel" \
                   "$work/evidence/mini-$case_name.$channel"; then
      echo "MINIAS_BUSYBOX_APPLET=FAIL arch=$arch name=$case_name difference=$channel" >&2
      ((case_errors += 1))
      ((applet_failures += 1))
    fi
  done
  if (( case_errors == 0 )); then
    echo "MINIAS_BUSYBOX_APPLET=PASS arch=$arch name=$case_name GNU_BFD=1 QEMU=2"
  fi
done

if (( applet_failures != 0 )); then
  echo "MINIAS_BUSYBOX_RUNTIME=FAIL arch=$arch C_owners=$replaced applets=${#cases[@]} failures=$applet_failures" | \
    tee "$work/evidence/summary.txt" >&2
  exit 1
fi
echo "MINIAS_BUSYBOX_RUNTIME=PASS arch=$arch C_owners=$replaced MiniAS_only=1 GNU_relink=1 applets=${#cases[@]}/${#cases[@]} runner=$qemu" | \
  tee "$work/evidence/summary.txt"

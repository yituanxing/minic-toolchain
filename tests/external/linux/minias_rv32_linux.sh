#!/usr/bin/env bash
# Real Linux 6.6.143 / ELF32 MiniAS-only assembler P0.  GNU GCC owns every C
# compilation, GNU binutils owns linking, and the unmodified Linux Image is
# booted under qemu-system-riscv32.  This does NOT certify RV32 P1.
set -Eeuo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
: "${MINIAS:?full path to built minic-as required}"
: "${WORK_DIR:?isolated evidence/build directory required}"
work=$(realpath -m "$WORK_DIR")
minias=$(realpath "$MINIAS")
test -x "$minias"
version=${LINUX_VERSION:-6.6.143}
[[ "$version" == 6.6.143 ]] || { echo "MINIAS_RV32=FAIL unsupported_linux=$version" >&2; exit 2; }
jobs=${MINIAS_LINUX_JOBS:-4}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'MINIAS_RV32=FAIL invalid_jobs' >&2; exit 2; }

for cmd in make curl tar sha256sum cmp diff python3 gcc riscv64-linux-gnu-gcc \
           riscv64-linux-gnu-as riscv64-linux-gnu-ld riscv64-linux-gnu-readelf \
           qemu-system-riscv32 cpio gzip fdtput fdtget sudo; do
    command -v "$cmd" >/dev/null || { echo "MINIAS_RV32=BLOCKED missing=$cmd" >&2; exit 2; }
done
mkdir -p "$work" "$work/evidence" "$work/failure-inputs" "$work/as-bin"
archive="$work/linux-$version.tar.xz"
src="$work/linux-$version"
# Do not certify a mutable preexisting tree as an original GNU Linux source.
# The archive is cacheable, but each run must extract a clean tree.
if [[ -n "${LINUX_SOURCE:-}" || -e "$src" ]]; then
    echo 'MINIAS_RV32=BLOCKED requires_clean_pinned_kernel_source_and_fresh_WORK_DIR' >&2
    exit 2
fi
if [[ ! -s "$archive" ]]; then
    curl -fsSL "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" -o "$archive"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"
src=$(realpath "$src")
test "$(make -s -C "$src" kernelversion)" = "$version"
sha256sum "$archive" "$minias" >"$work/evidence/source-and-assembler.sha256"
{
    printf 'repo_head=%s\nlinux_version=%s\n' "$(git -C "$repo" rev-parse HEAD)" "$version"
    for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-as \
                riscv64-linux-gnu-ld qemu-system-riscv32; do
        printf '%s: ' "$tool"
        "$tool" --version | sed -n '1p'
    done
} >"$work/evidence/toolchain-versions.txt"

bindir="$work/as-bin"
# Only GCC's assembler subprocess and Kbuild's target AS are overridden.
# Information-only GAS version probes may delegate, never production objects.
cat >"$bindir/riscv64-linux-gnu-as" <<'WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?}" "${MINIAS_AS_TRACE:?}" "${MINIAS_FAILURE_DIR:?}"
args=("$@")
opts=()
unsupported=()
input= output= query=0
for ((i=0;i<${#args[@]};++i)); do
    arg=${args[i]}
    case "$arg" in
        -march=*|-mabi=*) opts+=("$arg") ;;
        --version|--help|-v) query=1 ;;
        -o) if ((i+1<${#args[@]})); then output=${args[i+1]}; ((i+=1)); fi ;;
        -o?*) output=${arg#-o} ;;
        # MiniAS does not emit GNU linker-relax instructions by default.
        # Do not silently drop metadata, policy, ISA, or diagnostic options.
        -mno-relax|-w|--no-warn) ;;
        -*) unsupported+=("$arg") ;;
        *) [[ ! -f "$arg" ]] || input=$arg ;;
    esac
done
preserve_failure() {
    local reason=$1 id
    [[ -n "$input" && -f "$input" ]] || return 0
    id=$(printf '%s\n%s\n' "$PWD" "${output:-unknown}" | sha256sum | cut -c1-18)
    mkdir -p "$MINIAS_FAILURE_DIR"
    cp -- "$input" "$MINIAS_FAILURE_DIR/$id.s"
    {
        printf 'reason=%q\ncwd=%q\ninput=%q\noutput=%q\n' \
          "$reason" "$PWD" "$input" "$output"
        printf 'gnu_as_argv='; printf ' %q' "${args[@]}"; printf '\n'
        printf 'minias_argv='; printf ' %q' "$MINIAS" "${opts[@]}" \
          -o "$MINIAS_FAILURE_DIR/$id.o" "$MINIAS_FAILURE_DIR/$id.s"
        printf '\n'
    } >"$MINIAS_FAILURE_DIR/$id.replay.txt"
}
# Kconfig probes GCC/binutils capabilities by emitting generated /tmp/cc*.s
# into ephemeral .tmp_<pid>/tmp[.o], /tmp/tmp.<random>.o or /dev/null.
# These results affect the original kernel configuration, so GNU/MiniAS
# lanes must use the same GNU oracle. This exception is restricted to the
# Kbuild root and those exact scratch destinations: a genuine production
# object is under the build tree and MUST take the MiniAS-only route.
if [[ "$PWD" == "${MINIAS_KBUILD_OUT:?}" &&
      "$input" =~ ^/tmp/cc[[:alnum:]]+\.s$ &&
      ( "$output" =~ ^\.tmp_[0-9]+/tmp(\.o)?$ ||
        "$output" =~ ^/tmp/tmp\.[[:alnum:]]+\.o$ ||
        "$output" == /dev/null ) ]]; then
    printf 'delegate-kconfig-probe cwd=%q input=%q output=%q\n' \
      "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
    exec "${REAL_AS:-/usr/bin/riscv64-linux-gnu-as}" "$@"
fi
if ((query)); then
    # GCC's Kconfig as-version test includes ordinary GAS flags such as
    # --traditional-format, -fpic and -misa-spec=20191213.  They are safe to
    # forward ONLY for a proven version query whose output is discarded.
    # Production invocations still reject every unsupported option below.
    valid=0
    if [[ -z "$input" && -z "$output" ]]; then
        valid=1
    elif [[ "$output" == /dev/null ]] && \
         { [[ -z "$input" || "$input" == /tmp/cc*.s || "$input" == /dev/null ]]; }; then
        valid=1
    elif [[ "$input" == /tmp/cc*.s && "$output" =~ ^\.tmp_[0-9]+/tmp$ ]]; then
        valid=1
    fi
    if (( ! valid )); then
        echo 'MINIAS_RV32_AS=FAIL query_with_production_object' >&2
        preserve_failure query_with_production_object
        exit 2
    fi
    printf 'delegate-query argc=%s\n' "${#args[@]}" >>"$MINIAS_AS_TRACE"
    exec "${REAL_AS:-/usr/bin/riscv64-linux-gnu-as}" "$@"
fi
if ((${#unsupported[@]})); then
    {
        printf 'MINIAS_RV32_AS=FAIL unsupported_GNU_AS_flags'
        printf ' %q' "${unsupported[@]}"
        printf '\n'
    } >&2
    preserve_failure unsupported_flags
    exit 2
fi
if [[ -z "$input" || -z "$output" ]]; then
    printf 'MINIAS_RV32_AS=FAIL missing_input_or_output' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    preserve_failure missing_input_or_output
    exit 2
fi
printf 'assemble cwd=%q input=%q output=%q\n' "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
if "$MINIAS" "${opts[@]}" -o "$output" "$input"; then
    if [[ "$output" != /dev/null && ! -s "$output" ]]; then
        echo "MINIAS_RV32_AS=FAIL empty_object=$output" >&2
        printf 'fail cwd=%q input=%q output=%q\n' "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
        preserve_failure empty_output
        exit 1
    fi
    printf 'pass cwd=%q input=%q output=%q\n' "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
else
    status=$?
    printf 'fail cwd=%q input=%q output=%q\n' "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
    preserve_failure minias_failure
    exit "$status"
fi
WRAPPER
chmod +x "$bindir/riscv64-linux-gnu-as"
ln -s riscv64-linux-gnu-as "$bindir/as"

cross=riscv64-linux-gnu-
gnu_out="$work/gnu-out"
mini_out="$work/mini-out"
config() {
    local out=$1 cc=$2 as=$3
    local log="$work/evidence/$(basename "$out")-defconfig.log"
    if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross" \
        CC="$cc" AS="$as" defconfig >"$log" 2>&1; then
        echo "MINIAS_RV32_CONFIG=FAIL lane=$out stage=defconfig" >&2
        tail -n 75 "$log" >&2
        return 2
    fi
    "$src/scripts/config" --file "$out/.config" \
      -e NONPORTABLE -e ARCH_RV32I -d ARCH_RV64I \
      -d EFI -d RISCV_ISA_C -d FPU -d SMP -d VGA_CONSOLE
    log="$work/evidence/$(basename "$out")-olddefconfig.log"
    if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross" \
        CC="$cc" AS="$as" olddefconfig >"$log" 2>&1; then
        echo "MINIAS_RV32_CONFIG=FAIL lane=$out stage=olddefconfig" >&2
        tail -n 75 "$log" >&2
        return 2
    fi
    for expected in NONPORTABLE ARCH_RV32I 32BIT; do
        grep -Fxq "CONFIG_$expected=y" "$out/.config" || {
            echo "MINIAS_RV32_CONFIG=FAIL required=$expected lane=$out" >&2; return 1;
        }
    done
    for forbidden in ARCH_RV64I EFI RISCV_ISA_C FPU SMP VGA_CONSOLE; do
        grep -Fxq "# CONFIG_$forbidden is not set" "$out/.config" || {
            echo "MINIAS_RV32_CONFIG=FAIL forbidden=$forbidden lane=$out" >&2; return 1;
        }
    done
}

export MINIAS="$minias"
export MINIAS_AS_TRACE="$work/minias-as.trace"
export MINIAS_FAILURE_DIR="$work/failure-inputs"
export MINIAS_KBUILD_OUT="$mini_out"
export REAL_AS=$(command -v riscv64-linux-gnu-as)
mini_cc="riscv64-linux-gnu-gcc -B$bindir/"
mini_as="$bindir/riscv64-linux-gnu-as"
: >"$MINIAS_AS_TRACE"

# A false-positive assembler capability probe changes Linux's Kconfig and
# invalidates the GNU comparison.  Inspect BOTH real toolchains before Image.
config "$gnu_out" riscv64-linux-gnu-gcc riscv64-linux-gnu-as
config "$mini_out" "$mini_cc" "$mini_as"
cp "$gnu_out/.config" "$work/evidence/gnu.config"
cp "$mini_out/.config" "$work/evidence/mini.config"
if ! cmp -s "$gnu_out/.config" "$mini_out/.config"; then
    diff -u "$gnu_out/.config" "$mini_out/.config" \
      >"$work/evidence/kconfig.diff" || true
    echo 'MINIAS_RV32_CONFIG_PARITY=FAIL original_GNU_Kconfig_differs' >&2
    exit 3
fi
printf 'MINIAS_RV32_CONFIG_PARITY=PASS sha256=%s\n' \
  "$(sha256sum "$mini_out/.config" | cut -d' ' -f1)"

run_image() {
    local out=$1 cc=$2 as=$3 label=$4
    local log="$work/evidence/$label-kbuild.log"
    if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross" \
        CC="$cc" AS="$as" -j"$jobs" Image >"$log" 2>&1; then
        echo "MINIAS_RV32_KBUILD=FAIL lane=$label" >&2
        grep -nEi 'minic-as:|error:|undefined reference|Error [0-9]+|unsupported-' \
          "$log" | head -n 70 >&2 || true
        tail -n 80 "$log" >&2 || true
        exit 4
    fi
    test -s "$out/arch/riscv/boot/Image"
    test -s "$out/vmlinux"
    riscv64-linux-gnu-readelf -h "$out/vmlinux" >"$work/evidence/$label-vmlinux-elf.txt"
    grep -Eq 'Class:[[:space:]]+ELF32' "$work/evidence/$label-vmlinux-elf.txt"
    grep -Eq 'Machine:[[:space:]]+RISC-V' "$work/evidence/$label-vmlinux-elf.txt"
    sha256sum "$out/arch/riscv/boot/Image" >"$work/evidence/$label-Image.sha256"
    echo "MINIAS_RV32_KBUILD=PASS lane=$label Image_ELF32=1"
}

# GNU control is independent and must boot on this exact kernel/config/QEMU
# before a MiniAS failure can be attributed to the replacement assembler.
run_image "$gnu_out" riscv64-linux-gnu-gcc riscv64-linux-gnu-as gnu
RV32I_QEMU_IMAGE="$gnu_out/arch/riscv/boot/Image" \
RV32I_QEMU_WORK="$work/qemu-gnu" \
  bash "$repo/tools/ci/linux-rv32i-qemu-p0-v1.sh" \
  >"$work/evidence/gnu-qemu-p0.log" 2>&1 || {
    echo 'MINIAS_RV32_GNU_P0=BLOCKED original_GNU_Image_did_not_boot' >&2
    tail -n 100 "$work/evidence/gnu-qemu-p0.log" >&2
    exit 5
  }
echo 'MINIAS_RV32_GNU_P0=PASS original_GNU_Image=1'

run_image "$mini_out" "$mini_cc" "$mini_as" minias
python3 "$repo/tests/external/linux/minias_as_provenance_check.py" \
  --arch rv32 --kbuild-out "$mini_out" --trace "$MINIAS_AS_TRACE" \
  --compiler riscv64-linux-gnu-gcc --assembler riscv64-linux-gnu-as \
  --min-objects 1200
RV32I_QEMU_IMAGE="$mini_out/arch/riscv/boot/Image" \
RV32I_QEMU_WORK="$work/qemu-minias" \
  bash "$repo/tools/ci/linux-rv32i-qemu-p0-v1.sh" \
  >"$work/evidence/minias-qemu-p0.log" 2>&1 || {
    echo 'MINIAS_RV32_P0=FAIL kernel_or_guest' >&2
    tail -n 100 "$work/evidence/minias-qemu-p0.log" >&2
    exit 6
  }
printf 'MINIAS_RV32_P0=PASS GNU_GCC=1 MiniAS_only=1 GNU_ld=1 ELF32=1 QEMU_system_rv32=1 GNU_control=PASS head=%s\n' \
  "$(git -C "$repo" rev-parse HEAD)"
echo 'MINIAS_RV32_P1=NOT_TESTED separate_full_guest_contract_required'

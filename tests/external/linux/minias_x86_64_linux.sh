#!/usr/bin/env bash
# Linux 6.6.143: GCC frontend -> MiniAS as the *sole* production assembler ->
# GNU binutils linker -> actual x86-64 QEMU boot.  No successful ELF-only gate.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
version=${LINUX_VERSION:-6.6.143}
work=${WORK_DIR:-"$root/build/minias-linux-fourarch/x86_64"}
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
jobs=${MINIAS_LINUX_JOBS:-4}
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
timeout_seconds=${QEMU_TIMEOUT_SECONDS:-120}
shell_delay=${QEMU_SHELL_INPUT_DELAY_SECONDS:-10}

if [[ "$version" != 6.6.143 ]]; then
    echo "MINIAS_LINUX_FOURARCH_ERROR arch=x86_64 unsupported_linux=$version" >&2
    exit 2
fi
if [[ ! -x "$minias" ]]; then
    echo "MINIAS_LINUX_FOURARCH_ERROR arch=x86_64 missing_minias=$minias" >&2
    exit 2
fi
for tool in gcc as ld ar objcopy readelf curl tar sha256sum make cpio gzip \
            timeout "$qemu" python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINIAS_LINUX_FOURARCH_ERROR arch=x86_64 missing_tool=$tool" >&2
        exit 2
    fi
done

mkdir -p "$work" "$work/asm-bin" "$work/failure-inputs"
archive="$work/linux-$version.tar.xz"
src="$work/linux-$version"
image_out="$work/mini-out"
gnu_out="$work/gnu-out"
image="$image_out/arch/x86/boot/bzImage"
gnu_image="$gnu_out/arch/x86/boot/bzImage"
initramfs="$work/runtime-initramfs.cpio.gz"

# Immutable upstream bytes, not the host's distro-modified headers/kernel.
if [[ ! -s "$archive" ]]; then
    curl --fail --location --retry 3 --silent --show-error \
         "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" \
         --output "$archive"
fi
printf '%s  %s\n' \
    'dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932' \
    "$archive" | sha256sum --check
if [[ ! -d "$src" ]]; then
    tar -xJf "$archive" -C "$work"
fi

# This builder is architecture-neutral; RISCV_CC is its historical variable.
RISCV_CC=gcc BUILD_DIR="$work/initramfs" OUTPUT_INITRAMFS="$initramfs" \
    bash "$root/tests/external/linux/build_runtime_initramfs.sh"

# Configure the same kernel for GNU and MiniAS.  Enable the *real* runtime's
# network, eventfd, io_uring, initramfs and serial requirements explicitly.
configure_kernel() {
    local output=$1
    make -C "$src" O="$output" ARCH=x86_64 x86_64_defconfig
    "$src/scripts/config" --file "$output/.config" \
        -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
        -e TMPFS -e PROC_FS -e SYSFS -e SERIAL_8250 \
        -e SERIAL_8250_CONSOLE -e UNIX -e INET -e IPV6 \
        -e EVENTFD -e EPOLL -e INOTIFY_USER -e SIGNALFD \
        -e TIMERFD -e MEMFD_CREATE -e IO_URING -e FUTEX \
        -e NET -e PIDFD
    make -C "$src" O="$output" ARCH=x86_64 olddefconfig
}

configure_kernel "$image_out"

# GCC's -B selects this specific "as" executable.  Kbuild's direct AS path
# selects the same wrapper.  Only GNU GAS *information queries* are delegated.
# In particular 16/32-bit setup production assembly must not silently fall
# back to GNU AS just because MiniAS currently has a primarily ELF64 target.
real_as=$(command -v as)
wrapper="$work/asm-bin/as"
cat > "$wrapper" <<'WRAPPER'
#!/usr/bin/env bash
set -Eeuo pipefail
args=("$@")
output= input=
need_elf32=0
query=0 version_probe=0
for ((i=0; i<${#args[@]}; i++)); do
    arg=${args[i]}
    case "$arg" in
        --version) query=1; version_probe=1 ;;
        -v|--help) query=1 ;;
        --32) need_elf32=1 ;;
        -o)
            if (( i + 1 < ${#args[@]} )); then
                output=${args[i+1]}
                ((++i))
            fi
            ;;
        --output=*) output=${arg#--output=} ;;
        -*) ;;
        *) if [[ -f "$arg" ]]; then input=$arg; fi ;;
    esac
done
if (( query )); then
    # GAS `-v` may accompany a real assembly. Information-only probes are
    # delegated; any invocation that also names a production input/output
    # remains under MiniAS ownership (the verbosity flag is not forwarded).
    # Linux 6.6.143 scripts/as-version.sh runs GNU GCC with
    # `-Wa,--version -c -x assembler-with-cpp /dev/null -o /dev/null`.
    # Both paths point to the null device: this is an information query,
    # not production code; denying it breaks `syncconfig` before Kbuild.
    # The parser intentionally does not treat the /dev/null character device
    # as a source file (-f is false), so input may be empty for that probe.
    if [[ -z "$input" && -z "$output" ]] ||
       { (( version_probe )) &&
         { { [[ "$output" == /dev/null ]] &&
             [[ -z "$input" || "$input" == /tmp/cc*.s ]]; } ||
           { [[ "$output" =~ (^|/)\.tmp_[0-9]+/tmp$ ]] &&
             [[ "$input" == /tmp/cc*.s ]]; }; }; }; then
        printf 'delegate-query cwd=%q argc=%s\n' "$PWD" "${#args[@]}" \
            >>"${MINIAS_AS_TRACE:?}"
        exec "${REAL_AS:?}" "$@"
    fi
fi
if [[ -z "$input" || -z "$output" ]]; then
    printf 'MINIAS_X86_AS_WRAPPER_ERROR missing_input_or_output' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    exit 2
fi
printf 'assemble cwd=%q input=%q output=%q elf32=%s\n' \
    "$PWD" "$input" "$output" "$need_elf32" \
    >>"${MINIAS_AS_TRACE:?}"
minias_args=(--target=x86_64)
if (( need_elf32 )); then
    # GNU `as --32` is an i386 ELF32/EM_386/REL object request, not merely
    # a CPU lexical `.code32` switch.  The Target profile owns the object
    # ABI and i386 relocation names; ELF64 callers keep the default profile.
    minias_args+=(-march=i386 -mabi=i386)
fi
if ! "${MINIAS:?}" "${minias_args[@]}" -o "$output" "$input"; then
    printf 'fail cwd=%q input=%q output=%q\n' \
        "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
    if [[ -f "$input" ]]; then
        mkdir -p "${MINIAS_FAILURE_DIR:?}"
        owner_hash=$(printf '%s' "$output" | sha256sum)
        owner_hash=${owner_hash%% *}
        cp "$input" "$MINIAS_FAILURE_DIR/$owner_hash.s"
        printf 'failed-source owner=%q path=%q\n' "$output" \
            "$MINIAS_FAILURE_DIR/$owner_hash.s" >>"$MINIAS_AS_TRACE"
    fi
    exit 1
fi
if [[ ! -s "$output" ]]; then
    echo "MINIAS_X86_AS_WRAPPER_ERROR empty_output=$output" >&2
    exit 1
fi
if (( need_elf32 )) && ! readelf -h "$output" | grep -q 'Class:.*ELF32'; then
    echo "MINIAS_X86_AS_WRAPPER_ERROR required_ELF32_not_emitted input=$input output=$output" >&2
    printf 'fail-elf-class input=%q output=%q\n' "$input" "$output" >>"$MINIAS_AS_TRACE"
    exit 1
fi
printf 'pass cwd=%q input=%q output=%q\n' \
    "$PWD" "$input" "$output" >>"$MINIAS_AS_TRACE"
WRAPPER
chmod +x "$wrapper"
ln -sfn as "$work/asm-bin/x86_64-linux-gnu-as"
: >"$work/minias-as.trace"
export MINIAS="$minias"
export MINIAS_AS_TRACE="$work/minias-as.trace"
export MINIAS_FAILURE_DIR="$work/failure-inputs"
export REAL_AS="$real_as"

# GCC's discarded as-version probe is legal, but a real kernel .o may never
# reach GNU as via --version. Verify both cases before expensive Kbuild.
probe_input=$(mktemp /tmp/ccXXXXXX.s)
if ! MINIAS=/bin/false "$wrapper" --version -o /dev/null "$probe_input" \
    | grep -q 'GNU assembler'; then
    echo 'MINIAS_AS_VERSION_PROBE=FAIL arch=x86_64 harmless_query' >&2
    exit 1
fi
if MINIAS=/bin/false "$wrapper" --version -o "$image_out/probe-production.o" "$probe_input" \
    >/dev/null 2>&1; then
    echo 'MINIAS_AS_VERSION_PROBE=FAIL arch=x86_64 production_delegated' >&2
    exit 1
fi
rm -f "$probe_input"
echo 'MINIAS_AS_VERSION_PROBE=PASS arch=x86_64 query=1 production_gnu_as=0'

started=$(date +%s)
if ! make -C "$src" O="$image_out" ARCH=x86_64 \
    CC="gcc -B$work/asm-bin/" AS="$wrapper" -j"$jobs" -k bzImage \
    >"$work/kbuild-mini.log" 2>&1; then
    echo 'MINIAS_LINUX_FOURARCH_BUILD=FAIL arch=x86_64 stage=make-bzImage'
    echo 'MINIAS_LINUX_FOURARCH_DIAGNOSTIC=all-first-errors arch=x86_64'
    grep -nE 'minic-as:|MINIAS_X86_AS_WRAPPER_ERROR|(^| )error:|fatal error:|undefined reference|Error [0-9]+|No rule to make target|unsupported-|bad-' \
        "$work/kbuild-mini.log" | head -n 140 || true
    echo 'MINIAS_LINUX_FOURARCH_DIAGNOSTIC=unique-minias-error-classes arch=x86_64'
    grep -oE 'minic-as: [^[:cntrl:]]*' "$work/kbuild-mini.log" \
        | sed -E 's/:line=[0-9]+$/:line=.../' | LC_ALL=C sort -u \
        | head -n 120 || true
    echo 'MINIAS_LINUX_FOURARCH_DIAGNOSTIC=kbuild-tail arch=x86_64'
    tail -n 140 "$work/kbuild-mini.log" || true
    echo 'MINIAS_LINUX_FOURARCH_DIAGNOSTIC=assembler-last-failures arch=x86_64'
    grep -E '^fail' "$work/minias-as.trace" | tail -n 100 || true
    exit 1
fi
test -s "$image"
sha256sum "$image" >"$work/bzImage.sha256"
passes=$(grep -c '^pass ' "$work/minias-as.trace" || true)
test "$passes" -gt 1000
# Every Kbuild compiler/assembler-owned production object must have a
# successful MiniAS invocation at the same output path. This is a fail-closed
# provenance gate, not a fixed `no_gas_fallback=1` reporting assertion. Host
# tools, linker-combined objects and intentional GNU-AS version queries are
# classified separately by the shared manifest checker.
if ! python3 "$root/tests/external/linux/minias_as_provenance_check.py" \
        --arch x86_64 \
        --kbuild-out "$image_out" \
        --trace "$work/minias-as.trace" \
        --compiler gcc --assembler as --min-objects 1000; then
    echo 'MINIAS_LINUX_FOURARCH_PROVENANCE=FAIL arch=x86_64' >&2
    exit 1
fi
echo "MINIAS_LINUX_FOURARCH_BUILD=PASS arch=x86_64 image=$image mini_as_passes=$passes elapsed_s=$(( $(date +%s)-started ))"

run_qemu() {
    local flavor=$1 lane=$2 cmdline=$3 input_mode=$4
    local executable=$5
    local logfile="$work/qemu-$flavor/$lane.log"
    local status
    mkdir -p "$(dirname "$logfile")"
    set +e
    if [[ "$input_mode" == shell ]]; then
        (
            sleep "$shell_delay"
            printf '%s\n' 'mount -t proc proc /proc' \
                'echo PROC_CMDLINE_BEGIN' 'cat /proc/cmdline' \
                'echo PROC_CMDLINE_END' 'echo RDINIT_SH_OK' \
                'echo DONE_RDINIT' 'sync' 'poweroff -f'
            sleep 2
        ) | timeout --signal=TERM "$timeout_seconds" "$qemu" \
            -M q35 -cpu max -m 1G -smp 1 -nographic -no-reboot \
            -kernel "$executable" -initrd "$initramfs" -append "$cmdline" \
            >"$logfile" 2>&1
        status=$?
    else
        timeout --signal=TERM "$timeout_seconds" "$qemu" \
            -M q35 -cpu max -m 1G -smp 1 -nographic -no-reboot \
            -kernel "$executable" -initrd "$initramfs" -append "$cmdline" \
            </dev/null >"$logfile" 2>&1
        status=$?
    fi
    set -e
    if (( status != 0 && status != 124 && status != 143 )); then
        echo "MINIAS_LINUX_FOURARCH_BOOT=FAIL arch=x86_64 flavor=$flavor lane=$lane qemu_status=$status" >&2
        tail -n 180 "$logfile" >&2
        return 1
    fi
    echo "MINIAS_LINUX_FOURARCH_BOOT=RUN arch=x86_64 flavor=$flavor lane=$lane qemu_status=$status log=$logfile"
}

require_marker() {
    local log=$1 marker=$2
    if ! grep -E "$marker" "$log" >/dev/null; then
        echo "MINIAS_LINUX_FOURARCH_MISSING=FAIL arch=x86_64 marker=$marker log=$log" >&2
        tail -n 200 "$log" >&2
        exit 1
    fi
}

verify_boots() {
    local flavor=$1 executable=$2
    local common='console=ttyS0,115200 earlyprintk=serial,ttyS0 loglevel=8 panic=-1'
    local log="$work/qemu-$flavor/rdinit-init.log"
    run_qemu "$flavor" rdinit-init "$common rdinit=/init" none "$executable"
    for marker in "Linux version $version" \
        'Kernel command line:.*rdinit=/init' \
        'devtmpfs: initialized' \
        '(Trying to unpack rootfs image as initramfs|Unpacking initramfs)' \
        '(Run /init as init process|Starting init: /init)' \
        'INIT_PROC_MOUNT=PASS' 'INIT_SYSFS_MOUNT=PASS' \
        'INIT_DEVTMPFS_MOUNT=PASS' 'INIT_CMDLINE_READ=PASS' \
        'INIT_TMPFS_MOUNT=PASS' 'INIT_TMPFS_RW=PASS' \
        'USER_SHELL_OK' 'DONE_COMMANDS' \
        'MINIC_LINUX_RUNTIME_INIT_END fail=0'; do
        require_marker "$log" "$marker"
    done
    log="$work/qemu-$flavor/rdinit-sh.log"
    run_qemu "$flavor" rdinit-sh "$common rdinit=/bin/sh" shell "$executable"
    for marker in "Linux version $version" \
        'Kernel command line:.*rdinit=/bin/sh' \
        '(Run /bin/sh as init process|Starting init: /bin/sh)' \
        'MINIC_LINUX_RUNTIME_SH_READY' 'PROC_CMDLINE_BEGIN' \
        'console=ttyS0.*rdinit=/bin/sh' \
        'PROC_CMDLINE_END' 'RDINIT_SH_OK' 'DONE_RDINIT'; do
        require_marker "$log" "$marker"
    done
    log="$work/qemu-$flavor/p1.log"
    run_qemu "$flavor" p1 "$common rdinit=/init minic_runtime=p1" none "$executable"
    python3 "$root/tests/external/linux/runtime_v2_log_check.py" \
        --profile p1 --log "$log"
    echo "MINIAS_LINUX_FOURARCH_BOOT=PASS arch=x86_64 flavor=$flavor p0=2/2 p1=12/12"
}

# Build GNU's independent baseline even when MiniAS reaches decompression but
# fails before printk. Previously verify_boots exited before this build.
# GNU remains a diagnostic baseline, never a production-assembler fallback.
configure_kernel "$gnu_out"
gnu_build_rc=0
if ! make -C "$src" O="$gnu_out" ARCH=x86_64 -j"$jobs" bzImage \
    >"$work/kbuild-gnu.log" 2>&1; then
    echo 'MINIAS_LINUX_FOURARCH_GNU_BASELINE=FAIL arch=x86_64' >&2
    tail -n 160 "$work/kbuild-gnu.log" >&2
    gnu_build_rc=1
fi
if [[ ! -s "$gnu_image" ]]; then gnu_build_rc=1; fi

diagnose_early_boot() {
    local debug_dir="$work/diagnostics"
    local output status flavor obj
    mkdir -p "$debug_dir"
    echo 'MINIAS_LINUX_FOURARCH_DIAGNOSTIC=early-boot-cpu-and-gnu-object-diff arch=x86_64'
    # A silent reset after decompression may be a triple fault. Record the
    # actual CPU exception sequence rather than inferring it from serial logs.
    set +e
    timeout --signal=TERM 25 "$qemu" \
        -M q35 -cpu max -m 1G -smp 1 -nographic -no-reboot \
        -d int,cpu_reset,guest_errors -D "$debug_dir/qemu-cpu.log" \
        -kernel "$image" -initrd "$initramfs" \
        -append 'console=ttyS0,115200 earlyprintk=serial,ttyS0 loglevel=8 panic=-1 rdinit=/init' \
        </dev/null >"$debug_dir/qemu-serial.log" 2>&1
    status=$?
    set -e
    echo "MINIAS_LINUX_FOURARCH_DIAGNOSTIC=qemu-cpu-trace arch=x86_64 status=$status"
    tail -n 100 "$debug_dir/qemu-cpu.log" || true

    # Both objects come from the same pinned Linux sources and GCC Kbuild
    # flags. Preserve GNU-vs-MiniAS bytes and relocations for early owners.
    for obj in \
        arch/x86/kernel/head_64.o \
        arch/x86/kernel/head64.o \
        arch/x86/kernel/verify_cpu.o \
        arch/x86/kernel/idt.o \
        arch/x86/kernel/setup.o \
        arch/x86/boot/compressed/head_64.o \
        arch/x86/boot/compressed/pgtable_64.o; do
        if [[ ! -s "$image_out/$obj" || ! -s "$gnu_out/$obj" ]]; then
            continue
        fi
        output=${obj//\//_}
        objdump -drw -M intel "$image_out/$obj" >"$debug_dir/$output.mini.disasm"
        objdump -drw -M intel "$gnu_out/$obj" >"$debug_dir/$output.gnu.disasm"
        diff -u "$debug_dir/$output.gnu.disasm" \
            "$debug_dir/$output.mini.disasm" >"$debug_dir/$output.diff" || true
        echo "MINIAS_LINUX_FOURARCH_DIAGNOSTIC=object-pair arch=x86_64 owner=$obj"
    done
    for flavor in mini gnu; do
        if [[ "$flavor" == mini ]]; then output=$image_out; else output=$gnu_out; fi
        if [[ -s "$output/vmlinux" ]]; then
            readelf -W -h -S -l "$output/vmlinux" >"$debug_dir/$flavor-vmlinux-elf.log"
            nm -n "$output/vmlinux" >"$debug_dir/$flavor-vmlinux-symbols.log"
        fi
    done
    if [[ -s "$debug_dir/mini-vmlinux-symbols.log" && -s "$debug_dir/gnu-vmlinux-symbols.log" ]]; then
        diff -u "$debug_dir/gnu-vmlinux-symbols.log" \
            "$debug_dir/mini-vmlinux-symbols.log" >"$debug_dir/vmlinux-symbols.diff" || true
    fi
}

mini_boot_rc=0
( verify_boots mini "$image" ) || mini_boot_rc=$?
if (( mini_boot_rc != 0 )); then
    diagnose_early_boot
fi
gnu_boot_rc=0
if (( gnu_build_rc == 0 )); then
    ( verify_boots gnu "$gnu_image" ) || gnu_boot_rc=$?
else
    gnu_boot_rc=2
fi
if (( mini_boot_rc != 0 || gnu_boot_rc != 0 )); then
    echo "MINIAS_LINUX_FOURARCH_RUNTIME=FAIL arch=x86_64 mini_rc=$mini_boot_rc gnu_build_rc=$gnu_build_rc gnu_boot_rc=$gnu_boot_rc" >&2
    exit 1
fi
echo 'MINIAS_LINUX_FOURARCH_RUNTIME=PASS arch=x86_64 p0=2/2 p1=12/12 gnu_baseline=PASS gnu_bfd_link=1 no_gas_fallback=1'

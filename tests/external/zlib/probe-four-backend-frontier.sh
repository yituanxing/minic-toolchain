#!/bin/sh
# Original zlib 1.3.1: pinned upstream source, real GNU cross-oracles, and
# all-15-TU MiniC error census. Real project, not one synthetic opcode,
# is the iteration unit; blockers never count as runtime acceptance.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
: "${HOST_CC:=cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
commit=51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
sources='adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c'

# Fetch once for both ISAs. Reuse the proven x86 pin and blob identities.
if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
        "https://github.com/madler/zlib/archive/$commit.tar.gz" \
        -o "$work/zlib.tar.gz"; then
    echo 'FOUR_BACKEND_ZLIB_ORACLE=FAIL stage=download' >&2
    exit 1
fi
mkdir -p "$work/upstream"
tar -xzf "$work/zlib.tar.gz" -C "$work/upstream" --strip-components=1
src="$work/upstream"
for identity in \
    'zlib.h 8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0' \
    'deflate.c 012ea8148e8d21cf515a9ff47d4af719d049b276' \
    'zconf.h 62adc8d8431f2f9149ae0b1583915e21a28dd8b5' \
    'test/example.c c3521dd5934f716bc7c33fe1c5fd4a361c985e76'; do
    set -- $identity
    actual=$(git hash-object "$src/$1")
    if [ "$actual" != "$2" ]; then
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL stage=source_identity file=$1 actual=$actual" >&2
        exit 1
    fi
done
# Configure the original LP64/Linux headers once. GNU and MiniC consume the
# exact same resulting zconf.h; each cross GCC preprocesses the target TUs.
if ! (cd "$src" && CC="$HOST_CC" CFLAGS='-O0 -std=c11' ./configure) \
        >"$work/configure.log" 2>&1; then
    echo 'FOUR_BACKEND_ZLIB_ORACLE=FAIL stage=configure' >&2
    tail -n 25 "$work/configure.log" >&2
    exit 1
fi
if ! grep -F 'Checking for unistd.h... Yes.' "$work/configure.log" >/dev/null; then
    echo 'FOUR_BACKEND_ZLIB_ORACLE=FAIL stage=config_unistd' >&2
    exit 1
fi
printf 'FOUR_BACKEND_ZLIB_SOURCE=VERIFIED commit=%s TUs=15 config=real-upstream\n' "$commit"

passed=0
blocked=0
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    command -v "$gcc" >/dev/null || {
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target missing=$gcc" >&2; exit 1;
    }
    command -v "$qemu" >/dev/null || {
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target missing=$qemu" >&2; exit 1;
    }
    mkdir -p "$work/$target"
    # An independent GNU original-source reference must actually run.
    set --
    for source in $sources; do set -- "$@" "$src/$source"; done
    if ! "$gcc" -O0 -std=c11 -static -I"$src" "$src/test/example.c" "$@" \
            -o "$work/$target/oracle-example" >"$work/$target/oracle-build.out" \
            2>"$work/$target/oracle-build.err"; then
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target stage=GNU_build" >&2
        sed -n '1,30p' "$work/$target/oracle-build.err" >&2
        exit 1
    fi
    if ! timeout 60s "$qemu" "$work/$target/oracle-example" \
            >"$work/$target/oracle.out" 2>"$work/$target/oracle.err"; then
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target stage=GNU_QEMU" >&2
        sed -n '1,24p' "$work/$target/oracle.err" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_ZLIB_GNU=PASS target=$target original_example=1 TUs=15 QEMU_exit=0"

    compiled=0
    assembled=0
    target_blocked=0
    : >"$work/$target/rejects.log"
    set --
    for source in $sources; do
        stem=${source%.c}
        # GNU preprocessing is permitted. GCC may not compile any MiniC-owned
        # library TU in the candidate binary.
        if ! "$gcc" -E -P -std=c11 -I"$src" "$src/$source" \
                -o "$work/$target/$stem.i" >"$work/$target/$stem.cpp.out" \
                2>"$work/$target/$stem.cpp.err"; then
            echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target source=$source stage=GNU_preprocess" >&2
            sed -n '1,24p' "$work/$target/$stem.cpp.err" >&2
            exit 1
        fi
        set +e
        MINIC_BACKEND_CENSUS=1 MINIC_M1_TRACE=1 CORE_FAST_TRACE=0 \
            "$MINIC_CC" --target="$target-linux" -S \
            "$work/$target/$stem.i" -o "$work/$target/$stem.s" \
            >"$work/$target/$stem.minic.out" \
            2>"$work/$target/$stem.minic.err"
        status=$?
        set -e
        if [ "$status" -ne 0 ]; then
            if [ "$status" -ge 128 ]; then
                echo "FOUR_BACKEND_ZLIB=FAIL target=$target source=$source stage=MiniC_crash exit=$status" >&2
                sed -n '1,26p' "$work/$target/$stem.minic.err" >&2
                exit 1
            fi
            target_blocked=$((target_blocked + 1))
            grep 'MINIC_BACKEND_REJECT' "$work/$target/$stem.minic.err" \
                >>"$work/$target/rejects.log" || true
            first=$(grep -m 1 'MINIC_BACKEND_REJECT\|error:' \
                "$work/$target/$stem.minic.err" || true)
            echo "FOUR_BACKEND_ZLIB_TU=BLOCKED target=$target source=$source stage=MiniC status=$status $first"
            continue
        fi
        compiled=$((compiled + 1))
        if ! "$gcc" -c -x assembler "$work/$target/$stem.s" \
                -o "$work/$target/$stem.o" >"$work/$target/$stem.as.out" \
                2>"$work/$target/$stem.as.err"; then
            target_blocked=$((target_blocked + 1))
            echo "FOUR_BACKEND_ZLIB_TU=BLOCKED target=$target source=$source stage=GNU_assembler"
            sed -n '1,8p' "$work/$target/$stem.as.err"
            continue
        fi
        assembled=$((assembled + 1))
        set -- "$@" "$work/$target/$stem.o"
        echo "FOUR_BACKEND_ZLIB_TU=PASS target=$target source=$source compiled=$compiled assembled=$assembled"
    done
    echo "FOUR_BACKEND_ZLIB_TU_SUMMARY target=$target total=15 compiled=$compiled assembled=$assembled blocked=$target_blocked"
    # A single run collects first rejects across all 15 unmodified TUs;
    # group by Core kind and backend guard rather than 15 separate CI turns.
    if [ -s "$work/$target/rejects.log" ]; then
        awk '
            { key = ""; for (i=1;i<=NF;i++) if ($i ~ /^(stage|kind|backend_line)=/) key=key " " $i;
              if (key != "") count[key]++ }
            END { for (k in count) printf "%06d%s\n", count[k], k }
        ' "$work/$target/rejects.log" | sort -rn | head -n 30 |
            while IFS= read -r row; do
                echo "FOUR_BACKEND_ZLIB_ERROR_CLASS target=$target $row"
            done
    fi
    if [ "$target_blocked" -ne 0 ]; then
        echo "FOUR_BACKEND_ZLIB=BLOCKED target=$target TUs=15 compiled=$compiled assembled=$assembled blocked=$target_blocked"
        blocked=$((blocked + 1))
        continue
    fi
    # The executable contains ONLY MiniC-generated library objects; GNU
    # supplies the independent upstream client, CRT, assembler, and linker.
    if ! "$gcc" -O0 -std=c11 -static -I"$src" "$src/test/example.c" "$@" \
            -o "$work/$target/minic-example" >"$work/$target/link.out" \
            2>"$work/$target/link.err"; then
        echo "FOUR_BACKEND_ZLIB=FAIL target=$target stage=GNU_link"
        sed -n '1,35p' "$work/$target/link.err"
        failed=$((failed + 1))
        continue
    fi
    if ! timeout 60s "$qemu" "$work/$target/minic-example" \
            >"$work/$target/minic.out" 2>"$work/$target/minic.err" ||
       ! cmp -s "$work/$target/oracle.out" "$work/$target/minic.out" ||
       ! cmp -s "$work/$target/oracle.err" "$work/$target/minic.err"; then
        echo "FOUR_BACKEND_ZLIB=FAIL target=$target stage=runtime_example_differential"
        diff -u "$work/$target/oracle.out" "$work/$target/minic.out" || true
        diff -u "$work/$target/oracle.err" "$work/$target/minic.err" || true
        failed=$((failed + 1))
        continue
    fi
    echo "FOUR_BACKEND_ZLIB_EXAMPLE=PASS target=$target original_example=1 GCC_QEMU_byte_exact=1"

    # The second unmodified upstream client pressure-tests gzip streaming,
    # output identity and GCC/MiniC cross-decompression in both directions.
    if ! "$gcc" -O0 -std=c11 -static -I"$src" "$src/test/minigzip.c" "$@" \
            -o "$work/$target/minigzip-minic"; then
        echo "FOUR_BACKEND_ZLIB=FAIL target=$target stage=GNU_link_minigzip"; failed=$((failed+1)); continue
    fi
    set --
    for source in $sources; do set -- "$@" "$src/$source"; done
    if ! "$gcc" -O0 -std=c11 -static -I"$src" "$src/test/minigzip.c" "$@" \
            -o "$work/$target/minigzip-gnu"; then
        echo "FOUR_BACKEND_ZLIB_ORACLE=FAIL target=$target stage=GNU_build_minigzip" >&2; exit 1
    fi
    python3 - >"$work/$target/input.bin" <<'PY'
import sys
sys.stdout.buffer.write(bytes(range(256)) * 32)
sys.stdout.buffer.write(b'zlib-original-two-ISA-oracle:\x00\x01\x7f\xff' * 4096)
PY
    if ! timeout 60s "$qemu" "$work/$target/minigzip-gnu" -c \
            <"$work/$target/input.bin" >"$work/$target/gnu.gz" ||
       ! timeout 60s "$qemu" "$work/$target/minigzip-minic" -c \
            <"$work/$target/input.bin" >"$work/$target/mini.gz" ||
       ! cmp -s "$work/$target/gnu.gz" "$work/$target/mini.gz" ||
       ! timeout 60s "$qemu" "$work/$target/minigzip-minic" -d -c \
            <"$work/$target/gnu.gz" >"$work/$target/mini-roundtrip.bin" ||
       ! timeout 60s "$qemu" "$work/$target/minigzip-gnu" -d -c \
            <"$work/$target/mini.gz" >"$work/$target/gnu-roundtrip.bin" ||
       ! cmp -s "$work/$target/input.bin" "$work/$target/mini-roundtrip.bin" ||
       ! cmp -s "$work/$target/input.bin" "$work/$target/gnu-roundtrip.bin"; then
        echo "FOUR_BACKEND_ZLIB=FAIL target=$target stage=minigzip_differential"
        failed=$((failed + 1))
        continue
    fi
    echo "FOUR_BACKEND_ZLIB=PASS target=$target original_source=1 TUs=15 example=GNU_QEMU_byte_exact minigzip=GCC_cross_roundtrip"
    passed=$((passed + 1))
done
printf 'FOUR_BACKEND_ZLIB_FRONTIER=COMPLETE architectures=2 verified_runtime=%s blocked=%s failed=%s\n' \
    "$passed" "$blocked" "$failed"
if [ "$failed" -ne 0 ]; then exit 1; fi
if [ "${FOUR_BACKEND_ZLIB_REQUIRE:-0}" = 1 ]; then
    if [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; then
        echo "FOUR_BACKEND_ZLIB_ACCEPT=FAIL passed=$passed blocked=$blocked" >&2
        exit 1
    fi
    echo 'FOUR_BACKEND_ZLIB_ACCEPT=PASS targets=2 original_zlib=1 GNU_QEMU_cross_oracle=1'
fi

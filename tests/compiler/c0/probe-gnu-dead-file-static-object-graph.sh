#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
gcc=${CC:-gcc}

for fixture in gnu-dead-file-static-object-graph gnu_dead_file_static_object_graph_ext; do
    src="$root/tests/compiler/c0/$fixture.c"
    "$gcc" -std=gnu11 -E -P "$src" -o "$work/$fixture.i"
    "$minic" --target=x86_64-linux -S "$work/$fixture.i" -o "$work/$fixture.s"
    as --64 "$work/$fixture.s" -o "$work/$fixture.o"
    "$gcc" -std=gnu11 -O2 -fno-pie -fno-pic -c "$src" -o "$work/$fixture.gcc.o"
done

for object in "$work"/*.o; do
    if nm -u "$object" | grep -E \
        'minic_dead_static_object_must_not_resolve|minic_dead_graph_callback_must_not_resolve'; then
        echo "FAIL: static dead callback survives GNU ELF: $object" >&2
        exit 1
    fi
done

# An unreferenced `used` static object has no automatic section and MUST
# retain its callback's definition.  A linker section and file ASM provide
# independent roots, whereas the disconnected callback chain vanishes.
for symbol in retained_ops retained_callback retained_initcall; do
    nm -a "$work/gnu-dead-file-static-object-graph.o" |
        grep -q " $symbol\$"
done
for symbol in used_ops used_callback section_ops section_callback asm_only_data runtime_data; do
    nm -a "$work/gnu_dead_file_static_object_graph_ext.o" |
        grep -q " $symbol\$"
done
nm -a "$work/gnu_dead_file_static_object_graph_ext.o" |
    grep -q ' minic_dead_graph_public_alias$'
if nm -a "$work/gnu_dead_file_static_object_graph_ext.o" |
    grep -E ' unused_ops$| unused_callback$'; then
    echo 'FAIL: unreachable static object/callback still emitted' >&2
    exit 1
fi

# Optional exact original GNU .i files, in order: drivers/md/dm,
# net/netfilter/nf_nat_proto. Both assembler and GCC must agree on -U set.
if [ "${1:-}" != '' ] && [ "${2:-}" != '' ]; then
    i=0
    for src in "$1" "$2"; do
        i=$((i + 1))
        "$minic" --target=x86_64-linux -S "$src" -o "$work/real-$i.s"
        as --64 "$work/real-$i.s" -o "$work/real-$i.o"
        "$gcc" -x cpp-output -std=gnu11 -O2 -fno-pie -fno-pic \
            -mcmodel=kernel -mno-red-zone -c "$src" -o "$work/real-$i.gcc.o"
    done
    if nm -u "$work/real-1.o" | grep -q ' U dax_get_private$'; then
        echo 'FAIL: MiniC retained dead dm_dax_ops callback' >&2
        exit 1
    fi
    if nm -u "$work/real-2.o" | grep -q ' U crc32c$'; then
        echo 'FAIL: MiniC retained dead sctp_csum_ops callback' >&2
        exit 1
    fi
    if nm -u "$work/real-1.gcc.o" | grep -q ' U dax_get_private$'; then
        echo 'FAIL: GCC dm baseline disagreed' >&2
        exit 1
    fi
    if nm -u "$work/real-2.gcc.o" | grep -q ' U crc32c$'; then
        echo 'FAIL: GCC SCTP baseline disagreed' >&2
        exit 1
    fi
fi

echo 'PASS compiler/c0/gnu-dead-static-object-graph dead=pruned used=retained section=retained asm=retained volatile=live alias=live GNU-as=pass GNU-nm=differential'

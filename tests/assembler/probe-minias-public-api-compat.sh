#!/usr/bin/env bash
# The public MiniAS C ABI must remain usable while historical assemble_file*
# callers have not been inventoried outside this repository. The new public
# invocation policy may suppress ELF attributes, but must not change ISA,
# ABI, executable bytes, ELF width, or the existing compatibility entrypoints.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
binary=$(realpath "$MINIAS")
[[ -x "$binary" && "$binary" == */bin/minic-as ]] || {
    echo 'MINIAS_PUBLIC_ABI=BLOCKED expected_built_minic_as' >&2
    exit 2
}
build_root=${binary%/bin/minic-as}
test -s "$build_root/obj/assembler/src/invocation.o"
test -s "$build_root/obj/elf/src/relocatable_writer.o"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
printf '.text\n.globl public_api_probe\npublic_api_probe:\nnop\n' >"$work/input.s"

cat >"$work/public-client.c" <<'C'
#include "minias.h"

#include <stdio.h>
#include <string.h>

static char path[2048];

static const char *output(const char *dir, const char *file) {
    int n=snprintf(path,sizeof(path),"%s/%s",dir,file);
    if(n<0 || (size_t)n>=sizeof(path))return NULL;
    return path;
}

int main(int argc, char **argv) {
    MiniAsInvocation rv32={"riscv","rv32im","ilp32"};
    MiniAsInvocation invalid={"riscv","rv32i","lp64"};
    const char *src, *dir, *path_name;
    int result;
    if(argc!=3)return 2;
    src=argv[1];dir=argv[2];
#define CHECK(what, expr) do { \
    result=(expr); \
    if(result!=0){fprintf(stderr,"PUBLIC_API_FAIL %s rc=%d\n",what,result);return 1;} \
} while(0)
    path_name=output(dir,"legacy-default.o");
    if(!path_name)return 2;
    CHECK("file",minias_assemble_file(src,path_name,stderr));
    path_name=output(dir,"legacy-class32.o");
    if(!path_name)return 2;
    CHECK("file_class",minias_assemble_file_class(src,path_name,true,stderr));
    path_name=output(dir,"legacy-target32.o");
    if(!path_name)return 2;
    CHECK("file_target",minias_assemble_file_target(src,path_name,true,0U,stderr));
    path_name=output(dir,"legacy-strict32.o");
    if(!path_name)return 2;
    CHECK("file_target_isa",minias_assemble_file_target_isa(
        src,path_name,true,0U,32U,stderr));
    path_name=output(dir,"rv32-default-attrs.o");
    if(!path_name)return 2;
    CHECK("invocation",minias_assemble_invocation(src,path_name,&rv32,stderr));
    path_name=output(dir,"rv32-no-auto-attrs.o");
    if(!path_name)return 2;
    CHECK("invocation_with_policy",minias_assemble_invocation_with_policy(
        src,path_name,&rv32,true,stderr));
    path_name=output(dir,"rv32-bad-abi.o");
    if(!path_name)return 2;
    if(minias_assemble_invocation(src,path_name,&invalid,NULL)==0){
        fputs("PUBLIC_API_FAIL accepted_illegal_rv32_lp64\n",stderr);
        return 1;
    }
    return 0;
}
C

# Link the same already-built production objects as minic-as, minus only its
# executable CLI main. A C syntax-only probe would miss removed ABI symbols.
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/assembler/src" "$work/public-client.c" \
    "$build_root"/obj/elf/src/reader.o \
    "$build_root"/obj/elf/src/relocatable_writer.o \
    "$build_root"/obj/assembler/src/*.o -o "$work/public-client"
"$work/public-client" "$work/input.s" "$work"
test ! -e "$work/rv32-bad-abi.o"

for name in legacy-default legacy-class32 legacy-target32 legacy-strict32 \
            rv32-default-attrs rv32-no-auto-attrs; do
    test -s "$work/$name.o"
    readelf -h "$work/$name.o" >"$work/$name.header"
    grep -Eq 'Machine:[[:space:]]+RISC-V' "$work/$name.header"
    if [[ "$name" == legacy-default ]]; then
        grep -Eq 'Class:[[:space:]]+ELF64' "$work/$name.header"
    else
        grep -Eq 'Class:[[:space:]]+ELF32' "$work/$name.header"
    fi
done
if ! readelf -A "$work/rv32-default-attrs.o" | grep -q 'Tag_RISCV_arch'; then
    echo 'MINIAS_PUBLIC_ABI=FAIL missing_original_auto_arch_attribute' >&2
    exit 1
fi
if readelf -A "$work/rv32-no-auto-attrs.o" | grep -q 'Tag_RISCV_arch'; then
    echo 'MINIAS_PUBLIC_ABI=FAIL suppressed_policy_leaked_arch_attribute' >&2
    exit 1
fi
for name in rv32-default-attrs rv32-no-auto-attrs; do
    # Host objcopy may be built without a RISC-V BFD backend. GNU readelf
    # understands cross-target ELF and exposes the same emitted .text bytes.
    readelf -x .text "$work/$name.o" | sed -n '/^  0x/p' >"$work/$name.bytes"
done
test -s "$work/rv32-default-attrs.bytes"
cmp "$work/rv32-default-attrs.bytes" "$work/rv32-no-auto-attrs.bytes"
# ELF file offsets legitimately differ after removing attributes. Only
# class, machine, ABI and flags must be invariant, checked individually.
for field in Class Machine Flags; do
    sed -n "/^[[:space:]]*$field:/p" "$work/rv32-default-attrs.header" \
      >"$work/$field.default"
    sed -n "/^[[:space:]]*$field:/p" "$work/rv32-no-auto-attrs.header" \
      >"$work/$field.noattr"
    cmp "$work/$field.default" "$work/$field.noattr"
done
echo 'MINIAS_PUBLIC_ABI=PASS legacy_symbols=4 public_invocations=2 RV32_ELF32=1 auto_arch_policy_independent=1 illegal_ISA_ABI=rejected'

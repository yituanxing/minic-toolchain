#!/usr/bin/env bash
# Target ISA/ABI initialization must depend on its resolved private profile,
# never on the ELF writer's mutable output-only flags projection.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
binary=$(realpath "$MINIAS")
[[ -x "$binary" && "$binary" == */bin/minic-as ]] || {
    echo 'MINIAS_PROFILE_ELF_ISOLATION=BLOCKED expected_built_minic_as' >&2
    exit 2
}
build_root=${binary%/bin/minic-as}
test -s "$build_root/obj/assembler/src/invocation.o"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/profile-isolation.c" <<'C'
#include "minias_target_api.h"
#include "riscv_target.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The LoongArch Target privately starts its target-state structure with
 * these two semantic words; this white-box regression is deliberately
 * independent of the published ELF representation. */
typedef struct LoongSemanticState {
    uint32_t features;
    uint32_t abi;
} LoongSemanticState;

static int test_loong(const char *mabi, uint32_t expected) {
    MiniAsTargetSpec config;
    const MiniAsTargetOps *target=minias_target_loongarch64();
    char error[256]={0};
    void *original=NULL, *poisoned=NULL;
    LoongSemanticState a, b;
    int ok=0;
    if (!target->resolve_profile("loongarch64",mabi,&config,error,sizeof(error)) ||
        config.object.elf.initial_flags!=(UINT32_C(0x40)|expected) ||
        config.profile_size==0U ||
        !target->init_target_state(&config,&original,error,sizeof(error))) goto done;
    /* This intentionally makes ELF e_flags contradict the requested ABI. */
    config.object.elf.initial_flags=UINT32_C(0x40)|(expected%3U+1U);
    if (!target->init_target_state(&config,&poisoned,error,sizeof(error))) goto done;
    memcpy(&a,original,sizeof(a));
    memcpy(&b,poisoned,sizeof(b));
    ok=a.abi==expected && b.abi==expected && a.features==b.features;
done:
    if (original) target->destroy_target_state(original);
    if (poisoned) target->destroy_target_state(poisoned);
    if (!ok) fprintf(stderr,"PROFILE_ISOLATION_FAIL loong:%s %s\n",mabi,error);
    return ok;
}

static int test_riscv(const char *march,const char *mabi,
                      unsigned expected_xlen,int expected_e) {
    MiniAsTargetSpec config;
    const MiniAsTargetOps *target=minias_target_riscv();
    char error[256]={0};
    void *original=NULL, *poisoned=NULL;
    const MiniAsRiscvState *a, *b;
    int ok=0;
    int stage=0;
    if (!target->resolve_profile(march,mabi,&config,error,sizeof(error)) ||
        config.profile_size==0U ||
        !target->init_target_state(&config,&original,error,sizeof(error))) goto done;
    stage=1;
    /* Poison all ELF psABI/RVC flags without touching Target ISA/ABI. */
    config.object.elf.initial_flags^=UINT32_C(0xffffffff);
    if (!target->init_target_state(&config,&poisoned,error,sizeof(error))) goto done;
    stage=2;
    a=original;b=poisoned;
    ok=a->target_xlen==expected_xlen && b->target_xlen==expected_xlen &&
       a->rv32e_abi==(expected_e!=0) && b->rv32e_abi==(expected_e!=0) &&
       a->file_isa.features==b->file_isa.features &&
       a->file_isa.base_e==b->file_isa.base_e &&
       a->isa_explicit==b->isa_explicit &&
       strcmp(a->arch_attribute_isa,b->arch_attribute_isa)==0;
    if (!ok) fprintf(stderr,"PROFILE_DETAILS xlen=%u,%u e=%u,%u features=%llu,%llu base_e=%u,%u explicit=%u,%u attr=%s,%s\n",
        a->target_xlen,b->target_xlen,(unsigned)a->rv32e_abi,(unsigned)b->rv32e_abi,
        (unsigned long long)a->file_isa.features,(unsigned long long)b->file_isa.features,
        (unsigned)a->file_isa.base_e,(unsigned)b->file_isa.base_e,
        (unsigned)a->isa_explicit,(unsigned)b->isa_explicit,
        a->arch_attribute_isa,b->arch_attribute_isa);
done:
    if (original) target->destroy_target_state(original);
    if (poisoned) target->destroy_target_state(poisoned);
    if (!ok) fprintf(stderr,"PROFILE_ISOLATION_FAIL riscv:%s:%s stage=%d %s\n",
                     march,mabi,stage,error);
    return ok;
}

int main(void) {
    if (!test_loong("lp64s",1U) || !test_loong("lp64f",2U) ||
        !test_loong("lp64d",3U) ||
        !test_riscv("rv32e","ilp32e",32U,1) ||
        !test_riscv("rv32i","ilp32",32U,0) ||
        !test_riscv("rv64imac","lp64",64U,0)) return 1;
    puts("MINIAS_PROFILE_ELF_ISOLATION=PASS loong_abi=3 riscv_profiles=3 poisoned_elf_flags=6");
    return 0;
}
C

"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/assembler/src" "$work/profile-isolation.c" \
    "$build_root"/obj/elf/src/reader.o \
    "$build_root"/obj/elf/src/relocatable_writer.o \
    "$build_root"/obj/assembler/src/*.o -o "$work/profile-isolation"
"$work/profile-isolation"

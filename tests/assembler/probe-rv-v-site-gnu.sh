#!/usr/bin/env bash
# GAS permits V only inside .option arch,+v sites, including a pure-I file
# ISA. The local permission must survive inner push/pop and must not upgrade
# Tag_RISCV_arch or accidentally grant other unimplemented ISA modifiers.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_V_SITE_GNU=SKIP missing=$program"
        exit 0
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

exact=0
rejected=0
compare_section() {
    local name="$1" section="$2" producer
    for producer in gnu mini; do
        riscv64-linux-gnu-objcopy -O binary -j "$section" \
            "$work/$name.$producer.o" \
            "$work/$name.$producer.${section#.}.bin"
    done
    if ! cmp -s "$work/$name.gnu.${section#.}.bin" \
                "$work/$name.mini.${section#.}.bin"; then
        echo "MINIAS_RV_V_SITE_GNU=FAIL profile=$march case=$name section=$section" >&2
        exit 1
    fi
}

assemble_equal() {
    local name="$1" producer
    riscv64-linux-gnu-as "-march=$gnu_march" "-mabi=$mabi" -mno-relax \
        -o "$work/$name.gnu.o" "$work/$name.s"
    "$MINIAS" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.mini.o" "$work/$name.s"
    compare_section "$name" .text
    compare_section "$name" .riscv.attributes
    exact=$((exact + 1))
}

reject_both() {
    local name="$1"
    if riscv64-linux-gnu-as "-march=$gnu_march" "-mabi=$mabi" -mno-relax \
            -o "$work/$name.gnu.o" "$work/$name.s" \
            >"$work/$name.gnu.out" 2>"$work/$name.gnu.err"; then
        echo "MINIAS_RV_V_SITE_GNU=FAIL GNU accepted $name ($march)" >&2
        exit 1
    fi
    if "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$name.mini.o" "$work/$name.s" \
            >"$work/$name.mini.out" 2>"$work/$name.mini.err"; then
        echo "MINIAS_RV_V_SITE_GNU=FAIL MiniAS accepted $name ($march)" >&2
        exit 1
    fi
    rejected=$((rejected + 1))
}

reject_mini_option() {
    local name="$1"
    if "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$name.mini.o" "$work/$name.s" \
            >"$work/$name.mini.out" 2>"$work/$name.mini.err"; then
        echo "MINIAS_RV_V_SITE_GNU=FAIL MiniAS accepted $name ($march)" >&2
        exit 1
    fi
    grep -q 'unsupported-isa-option' "$work/$name.mini.err"
    rejected=$((rejected + 1))
}

for xlen in 32 64; do
    if test "$xlen" = 32; then mabi=ilp32; else mabi=lp64; fi
    march="rv${xlen}i"
    # GAS's unversioned rv*i is legacy I2p0 (implicitly allowing CSR and
    # Fence.I); MiniAS rv*i selects I2p1. Pin GAS's input ISA to I2p1 too.
    gnu_march="rv${xlen}i2p1"
    # Explicit .attribute arch pins the same 2p1 spelling in GAS and MiniAS.
    # No -march extension contains V or Zbb: only the lexical regions do.
    printf '.attribute arch,"rv%si2p1"\n.option norvc\n.text\nadd t0,t0,t1\n' \
        "$xlen" >"$work/rv$xlen-baseline.s"
    printf '.attribute arch,"rv%si2p1"\n.option norvc\n.text\n' \
        "$xlen" >"$work/rv$xlen-region.s"
    printf '%s\n' \
        '.option push' \
        '  .option arch, +v' \
        '  vsetvl x0,t5,t4' \
        '  vsetvli t0,x0,e8,m8,ta,ma' \
        '.option push' \
        '  .option arch,+zbb' \
        '  ctz a0,a0' \
        '.option pop' \
        '  vle8.v v0,(t3)' \
        '  vse8.v v8,(t3)' \
        '  vmv.v.i v16,7' \
        '.option pop' \
        'add t0,t0,t1' \
        '.option push' \
        '  .option arch,+v' \
        '  vsetvl x0,t5,t4' \
        '.option pop' >>"$work/rv$xlen-region.s"
    assemble_equal "rv$xlen-region"
    assemble_equal "rv$xlen-baseline"

    # GNU +v's lexical dependency closure is V, F, D and Zicsr. No F/D
    # selection belongs to the file's base ISA, including on RV32I.
    printf '.attribute arch,"rv%si2p1"\n.option norvc\n.text\n' \
        "$xlen" >"$work/rv$xlen-fd.s"
    printf '%s\n' \
        '.option push' \
        '.option arch,+v' \
        'fadd.s fa0,fa1,fa2' \
        'fadd.d fa3,fa4,fa5' \
        '.option pop' \
        'add t0,t0,t1' >>"$work/rv$xlen-fd.s"
    assemble_equal "rv$xlen-fd"

    printf '.attribute arch,"rv%si2p1"\n.option norvc\n.text\n' \
        "$xlen" >"$work/rv$xlen-zicsr.s"
    printf '%s\n' \
        '.option push' \
        '.option arch,+v' \
        'csrr t0,0xc01' \
        '.option pop' \
        'add t0,t0,t1' >>"$work/rv$xlen-zicsr.s"
    assemble_equal "rv$xlen-zicsr"
    for producer in gnu mini; do
        # The object ISA and every byte of Tag_File remain invariant even
        # while the local V/Zbb instruction parser accepts scoped forms.
        cmp "$work/rv$xlen-region.$producer.riscv.attributes.bin" \
            "$work/rv$xlen-baseline.$producer.riscv.attributes.bin"
        cmp "$work/rv$xlen-fd.$producer.riscv.attributes.bin" \
            "$work/rv$xlen-baseline.$producer.riscv.attributes.bin"
        # CSR introduces GNU's *privileged spec* tags 8/10, but neither
        # producer may upgrade the file's Tag_RISCV_arch from rv{32,64}i.
        test "$(readelf -A "$work/rv$xlen-zicsr.$producer.o" |
                  grep 'Tag_RISCV_arch:')" = \
             "$(readelf -A "$work/rv$xlen-baseline.$producer.o" |
                  grep 'Tag_RISCV_arch:')"
    done

    printf '.text\n.option push\n.option arch,+v\nvsetvl x0,t5,t4\n.option pop\nvsetvl x0,t5,t4\n' \
        >"$work/rv$xlen-outside-vsetvl.s"
    reject_both "rv$xlen-outside-vsetvl"
    printf '.text\n.option push\n.option arch,+v\nvmv.v.i v0,7\n.option pop\nvmv.v.i v0,7\n' \
        >"$work/rv$xlen-outside-vmv.s"
    reject_both "rv$xlen-outside-vmv"
    for instruction in 'fadd.s fa0,fa1,fa2' 'fadd.d fa0,fa1,fa2' \
                       'csrr t0,0xc01'; do
        name="rv$xlen-after-pop-$(printf '%s' "$instruction" | cut -d' ' -f1)"
        printf '.text\n.option push\n.option arch,+v\n%s\n.option pop\n%s\n' \
            "$instruction" "$instruction" >"$work/$name.s"
        reject_both "$name"
    done
    printf '.text\nvle8.v v0,(t3)\n' >"$work/rv$xlen-no-region.s"
    reject_both "rv$xlen-no-region"
    printf '.text\n.option push\n.option arch,+v\n.option push\n.option arch,+zbb\nctz a0,a0\n.option pop\nvsetvl x0,t5,t4\nctz a0,a0\n' \
        >"$work/rv$xlen-after-inner-pop.s"
    reject_both "rv$xlen-after-inner-pop"

    # Dependencies do not grant independent M/A/Zifencei capabilities.
    # GNU and MiniAS must reject these both before, within and after +v.
    for instruction in 'fence.i' 'mul a0,a1,a2' 'amoadd.w a0,a1,(a2)'; do
        mnemonic=$(printf '%s' "$instruction" | cut -d' ' -f1)
        for position in before during after; do
            name="rv$xlen-nondependency-$mnemonic-$position"
            case "$position" in
                before)
                    printf '.text\n%s\n.option arch,+v\nvsetvl x0,t5,t4\n' \
                        "$instruction" >"$work/$name.s" ;;
                during)
                    printf '.text\n.option push\n.option arch,+v\n%s\n.option pop\n' \
                        "$instruction" >"$work/$name.s" ;;
                after)
                    printf '.text\n.option push\n.option arch,+v\nvsetvl x0,t5,t4\n.option pop\n%s\n' \
                        "$instruction" >"$work/$name.s" ;;
            esac
            reject_both "$name"
        done
    done

    # +v1p0 is now a verified alias of +v. Other V revisions still fail
    # closed; this negative case must exercise an unsupported version.
    for suffix in +m +unknown +v1p1; do
        name="rv$xlen-unsupported-${suffix#+}"
        printf '.text\n.option arch,%s\nnop\n' "$suffix" >"$work/$name.s"
        reject_mini_option "$name"
    done
done

echo "MINIAS_RV_V_SITE_GNU=PASS profiles=RV32I/RV64I GNU_text_and_attributes_exact=$exact local_arch_unchanged=8 rejected=$rejected nested_push_pop=2 dependencies=V,F,D,Zicsr excluded=M,A,Zifencei"

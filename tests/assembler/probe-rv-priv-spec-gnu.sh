#!/usr/bin/env bash
# GNU's RISC-V privileged ELF version tags depend on source instruction
# forms, not the final opcode bits. Compare both instructions and canonical
# Tag_File bytes across RV32/RV64, including explicit source overrides.
set -Eeuo pipefail
: "${MINIAS:?exact MiniAS binary required}"
for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy cmp; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_PRIV_SPEC_GNU=SKIP missing=$program"
        exit 0
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cases=0
rejections=0
run_equal() {
    local name="$1" program="$2" section producer
    printf '%b' "$program" >"$work/$name.s"
    riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
        -o "$work/$name.gnu.o" "$work/$name.s"
    "$MINIAS" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.mini.o" "$work/$name.s"
    for section in .text .riscv.attributes; do
        for producer in gnu mini; do
            riscv64-linux-gnu-objcopy -O binary -j "$section" \
                "$work/$name.$producer.o" \
                "$work/$name.$producer.${section#.}.bin"
        done
        if ! cmp -s "$work/$name.gnu.${section#.}.bin" \
                    "$work/$name.mini.${section#.}.bin"; then
            echo "MINIAS_RV_PRIV_SPEC_GNU=FAIL profile=$march case=$name section=$section" >&2
            if command -v readelf >/dev/null 2>&1; then
                readelf -A "$work/$name.gnu.o" >&2 || true
                readelf -A "$work/$name.mini.o" >&2 || true
            fi
            exit 1
        fi
    done
    cases=$((cases + 1))
}

run_rejected() {
    local name="$1" program="$2"
    printf '%b' "$program" >"$work/$name.s"
    if riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
            -o "$work/$name.gnu.o" "$work/$name.s" \
            >"$work/$name.gnu.out" 2>"$work/$name.gnu.err"; then
        echo "MINIAS_RV_PRIV_SPEC_GNU=FAIL GNU accepted $name ($march)" >&2
        exit 1
    fi
    if "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$name.mini.o" "$work/$name.s" \
            >"$work/$name.mini.out" 2>"$work/$name.mini.err"; then
        echo "MINIAS_RV_PRIV_SPEC_GNU=FAIL MiniAS accepted $name ($march)" >&2
        exit 1
    fi
    rejections=$((rejections + 1))
}

for xlen in 32 64; do
    if test "$xlen" = 32; then
        march=rv32imafdc_zicsr_zifencei
        mabi=ilp32
    else
        march=rv64imafdc_zicsr_zifencei_zihintpause
        mabi=lp64
    fi

    # GAS infers 1.11 from these verified *source* forms. In particular,
    # CSRR and CSRW have different encodings but the same metadata policy.
    run_equal "rv$xlen-csrr" '.text\ncsrr t0,0xc01\n'
    run_equal "rv$xlen-csrw" '.text\ncsrw 0x300,t0\n'
    run_equal "rv$xlen-csrrsi" '.text\ncsrrsi t0,0x300,1\n'
    run_equal "rv$xlen-sret" '.text\nsret\n'
    run_equal "rv$xlen-wfi" '.text\nwfi\n'
    run_equal "rv$xlen-sfence" '.text\nsfence.vma\n'

    # The system opcode alone does not infer a privileged ISA version.
    run_equal "rv$xlen-nop" '.text\nnop\n'
    run_equal "rv$xlen-ecall" '.text\necall\n'
    run_equal "rv$xlen-ebreak" '.option norvc\n.text\nebreak\n'
    run_equal "rv$xlen-fcsr" '.text\nfrcsr t0\n'

    # Source tags select known GNU versions, including the revision tag.
    run_equal "rv$xlen-explicit-112" \
        '.attribute 8,1\n.attribute 10,12\n.text\ncsrr t0,0xc01\n'
    run_equal "rv$xlen-explicit-named-110" \
        '.attribute priv_spec,1\n.attribute priv_spec_minor,10\n.text\nnop\n'
    run_equal "rv$xlen-explicit-191" \
        '.attribute 12,1\n.attribute 10,9\n.attribute 8,1\n.text\nsret\n'
    run_equal "rv$xlen-explicit-zero-with-csr" \
        '.attribute 8,0\n.attribute 10,0\n.text\ncsrr t0,0xc01\n'
    run_equal "rv$xlen-explicit-zero-with-nop" \
        '.attribute 8,0\n.attribute 10,0\n.text\nnop\n'

    # GNU checks incomplete/unknown tuples and disallows privileged ELF
    # version directives after *any* instruction, including plain NOP.
    run_rejected "rv$xlen-partial-major" '.attribute 8,1\n.text\nnop\n'
    run_rejected "rv$xlen-partial-minor" '.attribute 10,12\n.text\ncsrr t0,0xc01\n'
    run_rejected "rv$xlen-unknown-revision" \
        '.attribute 8,1\n.attribute 10,11\n.attribute 12,1\n.text\nnop\n'
    run_rejected "rv$xlen-late-ordinary" \
        '.text\nnop\n.attribute 8,1\n.attribute 10,11\n'
    run_rejected "rv$xlen-late-csr" \
        '.text\ncsrr t0,0xc01\n.attribute priv_spec_minor,12\n'
done

echo "MINIAS_RV_PRIV_SPEC_GNU=PASS exact_text_and_attributes=$cases rejected=$rejections profiles=rv32,rv64"

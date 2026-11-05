#!/usr/bin/env bash
# Exact GNU GAS 2.42 RISC-V positive .option relax contract: a
# source-selected linker-relax site retains its primary relocation and a
# zero-width R_RISCV_RELAX marker; executable alignment reserves worst-case
# NOPs with R_RISCV_ALIGN instead of ordinary current-location padding.
# Both RV32I and RV64I are tested with GNU's initial -mno-relax so that the
# only opt-in comes from source .option relax and is restored by push/pop.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact MiniAS binary}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-ld \
               riscv64-linux-gnu-objcopy readelf cmp awk; do
    if ! command -v "$program" >/dev/null 2>&1; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_RV_LINK_RELAX=BLOCKED missing=$program" >&2
            exit 1
        fi
        echo "MINIAS_RV_LINK_RELAX=SKIP missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

write_case() {
    case "$1" in
        call_align_pop)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option relax
call external_target
.p2align 3
.option pop
.globl after_relax_align
after_relax_align:
call external_target
ASM
            ;;
        la_local)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option relax
lla a0,.Ltarget
.Ltarget:
.globl after_local_lla
after_local_lla:
ret
.option pop
ASM
            ;;
        la_local_final_relax)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option relax
lla a0,.Ltarget
.Ltarget:
.globl after_local_lla
after_local_lla:
ret
ASM
            ;;
        pic_got)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option pic
.option relax
la a0,external_data
.option pop
ret
ASM
            ;;
        absolute)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option relax
lui a0,%hi(external_data)
addi a0,a0,%lo(external_data)
.option pop
ret
ASM
            ;;
        nested_toggle)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option relax
call external_target
.option push
.option norelax
call external_target
.option pop
call external_target
.option pop
call external_target
ASM
            ;;
        align_rvc)
            cat <<'ASM'
.text
.globl relax_probe
relax_probe:
.option push
.option rvc
.option relax
c.li a0,1
.p2align 3
.option pop
.globl after_relax_align
after_relax_align:
addi a1,a2,123
ASM
            ;;
        *) return 1;;
    esac
}

# Normalize only the target relocation identities and offsets. GNU and
# MiniAS intentionally use different private PCREL .L anchor spellings;
# marker placement and primary relocation presence are what this probe
# must compare independently of private Symbol names and indices.
relocation_signature() {
    readelf -Wr "$1" | awk '$3 ~ /^R_RISCV_/ {print $1 ":" $3}'
}

# A paired GNU linker oracle is needed because R_RISCV_RELAX alone proves
# only ELF metadata: --relax must remove CALL bytes and excess ALIGN NOPs
# while preserving every later symbol offset and target instruction.
linked_symbol_value() {
    readelf -Ws "$1" | awk -v symbol="$2" '$8 == symbol {print $2; exit}'
}

cases=0
for bits in 32 64; do
    if [[ $bits == 32 ]]; then mabi=ilp32; else mabi=lp64; fi
    march="rv${bits}i2p1"
    if [[ $bits == 32 ]]; then ld_emulation=elf32lriscv; else ld_emulation=elf64lriscv; fi
    cat >"$work/$bits-definitions.s" <<'ASM'
.text
.globl external_target
external_target:
    ret
.section .rodata
.globl external_data
external_data:
    .word 42
ASM
    riscv64-linux-gnu-as --no-pad-sections "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$work/$bits-definitions.o" "$work/$bits-definitions.s"
    for scenario in call_align_pop la_local la_local_final_relax pic_got absolute nested_toggle align_rvc; do
        stem="$work/$bits-$scenario"
        write_case "$scenario" >"$stem.s"
        riscv64-linux-gnu-as --no-pad-sections "-march=$march" "-mabi=$mabi" \
            -mno-relax -o "$stem.gnu.o" "$stem.s"
        "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
            -o "$stem.mini.o" "$stem.s"
        for producer in gnu mini; do
            riscv64-linux-gnu-objcopy -j .text -O binary \
                "$stem.$producer.o" "$stem.$producer.text.bin"
            relocation_signature "$stem.$producer.o" \
                >"$stem.$producer.relocs"
        done
        if ! cmp -s "$stem.gnu.text.bin" "$stem.mini.text.bin" ||
           ! cmp -s "$stem.gnu.relocs" "$stem.mini.relocs"; then
            echo "MINIAS_RV_LINK_RELAX=FAIL xlen=$bits scenario=$scenario" >&2
            for producer in gnu mini; do
                echo "MINIAS_RV_LINK_RELAX=DIAG $producer relocations:" >&2
                readelf -Wr "$stem.$producer.o" >&2
                readelf -x .text "$stem.$producer.o" >&2
            done
            exit 1
        fi
        # GNU resolves disposable same-section .L PCREL pairs after source
        # .option pop restores final -mno-relax. Unlike external addresses,
        # the local pair therefore has no surviving RELAX markers.
        if [[ $scenario != align_rvc && $scenario != la_local ]] &&
           ! grep -q 'R_RISCV_RELAX' "$stem.gnu.relocs"; then
            echo "MINIAS_RV_LINK_RELAX=FAIL no-positive-gnu-marker scenario=$scenario" >&2
            exit 1
        fi
        # GNU 2.42 marks BOTH parts of a PIC `la` sequence. A standalone
        # GOT_HI20 relocation does not carry the source-macro permission;
        # here the MCInst itself establishes that this is an `la` pseudo.
        if [[ $scenario == pic_got ]] &&
           [[ $(grep -c R_RISCV_RELAX "$stem.gnu.relocs") != 2 ]]; then
            echo "MINIAS_RV_LINK_RELAX=FAIL gnu-pic-la-marker-pair" >&2
            exit 1
        fi
        if [[ $scenario == call_align_pop || $scenario == align_rvc ]]; then
            if ! grep -q 'R_RISCV_ALIGN' "$stem.gnu.relocs"; then
                echo "MINIAS_RV_LINK_RELAX=FAIL no-gnu-alignment scenario=$scenario" >&2
                exit 1
            fi
        fi
        for link_mode in relax no-relax; do
            for producer in gnu mini; do
                riscv64-linux-gnu-ld -m "$ld_emulation" \
                    "--$link_mode" -e relax_probe \
                    -o "$stem.$producer.$link_mode.elf" \
                    "$stem.$producer.o" "$work/$bits-definitions.o"
                riscv64-linux-gnu-objcopy -j .text -O binary \
                    "$stem.$producer.$link_mode.elf" \
                    "$stem.$producer.$link_mode.text.bin"
            done
            if ! cmp -s "$stem.gnu.$link_mode.text.bin" \
                        "$stem.mini.$link_mode.text.bin"; then
                echo "MINIAS_RV_LINK_RELAX=FAIL linked-text xlen=$bits scenario=$scenario mode=$link_mode" >&2
                for producer in gnu mini; do
                    readelf -x .text "$stem.$producer.$link_mode.elf" >&2
                done
                exit 1
            fi
            if [[ $scenario == call_align_pop || $scenario == align_rvc ]]; then
                for producer in gnu mini; do
                    value=$(linked_symbol_value "$stem.$producer.$link_mode.elf" after_relax_align)
                    if [[ -z $value ]]; then
                        echo "MINIAS_RV_LINK_RELAX=FAIL missing-linked-align-symbol" >&2
                        exit 1
                    fi
                    if [[ $producer == gnu ]]; then
                        gnu_value=$value
                    elif [[ $value != "$gnu_value" ]]; then
                        echo "MINIAS_RV_LINK_RELAX=FAIL linked-align-symbol xlen=$bits mode=$link_mode gnu=$gnu_value mini=$value" >&2
                        exit 1
                    fi
                    if [[ $link_mode == relax && $((16#$value % 8)) -ne 0 ]]; then
                        echo "MINIAS_RV_LINK_RELAX=FAIL relaxed-align-contract xlen=$bits producer=$producer addr=$value" >&2
                        exit 1
                    fi
                done
            elif [[ $scenario == la_local || $scenario == la_local_final_relax ]]; then
                if [[ $(linked_symbol_value "$stem.gnu.$link_mode.elf" after_local_lla) != \
                      $(linked_symbol_value "$stem.mini.$link_mode.elf" after_local_lla) ]]; then
                    echo "MINIAS_RV_LINK_RELAX=FAIL local-symbol-after-lla xlen=$bits mode=$link_mode" >&2
                    exit 1
                fi
            fi
        done
        ((cases+=1))
    done
done
echo "MINIAS_RV_LINK_RELAX=PASS RV32_RV64=2 scenarios=7 exact_object_and_linked_text=$cases link_modes=relax,no-relax symbol_alignment=8"

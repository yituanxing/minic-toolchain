#!/usr/bin/env bash
# A single three-target contract for deferred LEB expressions under linker
# byte deletion.  ULEB's SET/SUB or ADD/SUB pair can represent a plain
# symbol difference; no ELF LEB relocation can encode division, bit masking,
# or an unbound location-counter term after an intervening RELAX site.
# These expressions must not be frozen to stale assembler-time constants.
set -Eeuo pipefail

: "${MINIAS:?Point MINIAS at the exact MiniAS binary}"
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

for arch in rv32 rv64 loongarch64; do
    case "$arch" in
        rv32)
            target=(--target=riscv -march=rv32i2p1 -mabi=ilp32)
            instruction='call external_target'
            reloc_pattern='R_RISCV_(SET|SUB)_ULEB128'
            marker_pattern='R_RISCV_RELAX'
            ;;
        rv64)
            target=(--target=riscv -march=rv64i2p1 -mabi=lp64)
            instruction='call external_target'
            reloc_pattern='R_RISCV_(SET|SUB)_ULEB128'
            marker_pattern='R_RISCV_RELAX'
            ;;
        loongarch64)
            target=(--target=loongarch64 -march=loongarch64 -mabi=lp64d)
            instruction='la.local $a0,.Lend'
            reloc_pattern='R_LARCH_(ADD|SUB)_ULEB128'
            marker_pattern='R_LARCH_RELAX'
            ;;
    esac

    bad=0
    good=0
    for expression in divided signed_mask dot; do
        case "$expression" in
            divided)
                directive='.uleb128 (.Lend-.Lstart)/2'
                section='.section .debug_rnglists,"",@progbits'
                ;;
            signed_mask)
                directive='.sleb128 (.Lend-.Lstart)&7'
                section='.section .debug_rnglists,"",@progbits'
                ;;
            dot)
                directive='.uleb128 .-.Lstart'
                section='.text'
                ;;
        esac
        for source_mode in relax norelax; do
            source="$work/$arch-$expression-$source_mode.s"
            object="$work/$arch-$expression-$source_mode.o"
            {
                printf '.text\n.option %s\n.Lstart:\n  %s\n.Lend:\n' \
                    "$source_mode" "$instruction"
                printf '%s\n  %s\n.option norelax\n' "$section" "$directive"
            } >"$source"
            if [[ $source_mode == relax ]]; then
                if "$MINIAS" "${target[@]}" -o "$object" "$source" \
                    >"$work/stdout" 2>"$work/stderr"; then
                    echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL unexpected_accept arch=$arch expr=$expression" >&2
                    exit 1
                fi
                # This is a deliberate semantic rejection, not an unrelated
                # unrecognized-source or unsupported-ISA parser failure.
                if ! grep -Eiq 'link.relax|linker.motion|link.motion|unrepresentable' \
                    "$work/stderr"; then
                    echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL wrong_diagnostic arch=$arch expr=$expression" >&2
                    sed -n '1,12p' "$work/stderr" >&2
                    exit 1
                fi
                bad=$((bad+1))
            else
                if ! "$MINIAS" "${target[@]}" -o "$object" "$source" \
                    >"$work/stdout" 2>"$work/stderr"; then
                    echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL stable_rejected arch=$arch expr=$expression" >&2
                    sed -n '1,12p' "$work/stderr" >&2
                    exit 1
                fi
                good=$((good+1))
            fi
        done
    done

    # These are all positive RELAX cases: cancellation and arithmetic
    # identities must not be misdiagnosed as linker-motion dependencies.
    # When a nonzero symbol difference survives simplification, preserve
    # its real target-specific ADD/SUB or SET/SUB ULEB relocation pair.
    for expression in cancel simple_pair zero_mul zero_and identity_div \
        identity_mul identity_or identity_xor identity_shl abs_tail; do
        source="$work/$arch-$expression.s"
        object="$work/$arch-$expression.o"
        expected_pair=true
        case "$expression" in
            cancel)
                directive='.uleb128 (.Lend-.Lstart)-(.Lend-.Lstart)'
                expected_pair=false
                ;;
            simple_pair) directive='.uleb128 .Lend-.Lstart' ;;
            zero_mul)
                directive='.uleb128 (.Lend-.Lstart)*0'
                expected_pair=false
                ;;
            zero_and)
                directive='.uleb128 (.Lend-.Lstart)&0'
                expected_pair=false
                ;;
            identity_div) directive='.uleb128 (.Lend-.Lstart)/1' ;;
            identity_mul) directive='.uleb128 (.Lend-.Lstart)*1' ;;
            identity_or) directive='.uleb128 (.Lend-.Lstart)|0' ;;
            identity_xor) directive='.uleb128 (.Lend-.Lstart)^0' ;;
            identity_shl) directive='.uleb128 (.Lend-.Lstart)<<0' ;;
            abs_tail) directive='.uleb128 (.Lend-.Lstart)+ABS_CONST' ;;
        esac
        {
            printf '.equ ABS_CONST,5\n.text\n.option relax\n.Lstart:\n  %s\n.Lend:\n' \
                "$instruction"
            printf '.section .debug_rnglists,"",@progbits\n  %s\n.option norelax\n' \
                "$directive"
        } >"$source"
        if ! "$MINIAS" "${target[@]}" -o "$object" "$source" \
            >"$work/stdout" 2>"$work/stderr"; then
            echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL valid_rejected arch=$arch expr=$expression" >&2
            sed -n '1,12p' "$work/stderr" >&2
            exit 1
        fi
        readelf -rW "$object" >"$work/$arch-$expression.reloc"
        if ! grep -Eq "$marker_pattern" "$work/$arch-$expression.reloc"; then
            echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL missing_relax_marker arch=$arch expr=$expression" >&2
            exit 1
        fi
        if [[ $expected_pair == true ]]; then
            if [[ $(grep -Ec "$reloc_pattern" "$work/$arch-$expression.reloc" || true) != 2 ]]; then
                echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL missing_uleb_pair arch=$arch" >&2
                exit 1
            fi
        elif grep -Eq "$reloc_pattern" "$work/$arch-$expression.reloc"; then
            echo "MINIAS_LINK_MOTION_LEB_EXPR=FAIL unneeded_uleb_pair arch=$arch" >&2
            exit 1
        fi
        good=$((good+1))
    done

    echo "MINIAS_LINK_MOTION_LEB_EXPR=PASS arch=$arch rejected_unsafe=$bad stable_and_simplified=$good"
done
echo 'MINIAS_LINK_MOTION_LEB_EXPR=PASS targets=3 unsafe=9 stable_or_valid=39'

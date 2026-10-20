#!/bin/sh
# Original Lua 5.5.0: all 34 real translation units, no substituted sources.
# Discovery exhaustively lists target backend failures; only full GNU/QEMU
# runtime differential of MiniC-owned objects counts as acceptance.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
release=5.5.0
expected=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
        "https://www.lua.org/ftp/lua-$release.tar.gz" \
        -o "$work/lua.tar.gz"; then
    echo "FOUR_BACKEND_LUA_ORACLE=FAIL stage=download" >&2
    exit 1
fi
printf '%s  %s\n' "$expected" "$work/lua.tar.gz" | sha256sum -c - >/dev/null
mkdir -p "$work/upstream"
tar -xzf "$work/lua.tar.gz" -C "$work/upstream"
vendor="$work/upstream/lua-$release"
if [ -d "$vendor/src" ]; then src="$vendor/src"; else src="$vendor"; fi
sources='lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c lua.c luac.c'
# Match mature x86's deterministic original-Lua pressure script.
script='local t={3,1,2}; table.sort(t); assert(table.concat(t,",")=="1,2,3"); local function f(n) return n*7 end; assert(f(6)==42); local co=coroutine.create(function() coroutine.yield(21); return 42 end); local ok,x=coroutine.resume(co); assert(ok and x==21); ok,x=coroutine.resume(co); assert(ok and x==42); assert(math.floor(42.9)==42); print("LUA_CROSS",string.reverse("CiniM"),string.format("%d",f(6)))'
echo "FOUR_BACKEND_LUA_SOURCE=VERIFIED release=$release sha256=$expected total_TUs=34"
passed=0
blocked=0
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    mkdir -p "$work/$target"
    set --
    for file in $sources; do
        [ "$file" = luac.c ] && continue
        set -- "$@" "$src/$file"
    done
    if ! "$gcc" -O0 -std=c99 -static -DLUA_USE_LINUX -I"$src" \
            "$@" -lm -ldl -o "$work/$target/lua-gnu" \
            >"$work/$target/gnu-build.out" 2>"$work/$target/gnu-build.err"; then
        echo "FOUR_BACKEND_LUA_ORACLE=FAIL target=$target stage=GNU_original_build" >&2
        sed -n '1,36p' "$work/$target/gnu-build.err" >&2
        exit 1
    fi
    if ! timeout 60s "$qemu" "$work/$target/lua-gnu" -e "$script" \
            >"$work/$target/gnu.out" 2>"$work/$target/gnu.err"; then
        echo "FOUR_BACKEND_LUA_ORACLE=FAIL target=$target stage=GNU_QEMU" >&2
        sed -n '1,30p' "$work/$target/gnu.err" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_LUA_GNU=PASS target=$target original_Lua_5.5.0=1 QEMU_runtime=1"

    compiled=0; assembled=0; target_blocked=0
    # One source excerpt per distinct frontend-error location, not a
    # repeated 34-file stderr dump. This shows actual GNU-preprocessed
    # declarations and prevents guessing cross-glibc syntax issues.
    parse_context_lines=' '
    : >"$work/$target/rejects.log"
    for file in $sources; do
        stem=${file%.c}
        if ! "$gcc" -E -P -std=c99 -DLUA_USE_LINUX -I"$src" \
                "$src/$file" -o "$work/$target/$stem.i" \
                >"$work/$target/$stem.cpp.out" 2>"$work/$target/$stem.cpp.err"; then
            echo "FOUR_BACKEND_LUA=FAIL target=$target source=$file stage=GNU_preprocess" >&2
            sed -n '1,25p' "$work/$target/$stem.cpp.err" >&2
            failed=$((failed + 1))
            break
        fi
        set +e
        MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 CORE_FAST_TRACE=0 \
            "$MINIC_CC" --target="$target-linux" -S "$work/$target/$stem.i" \
            -o "$work/$target/$stem.s" >"$work/$target/$stem.minic.out" \
            2>"$work/$target/$stem.minic.err"
        status=$?
        set -e
        if [ "$status" -ne 0 ]; then
            if [ "$status" -ge 128 ]; then
                echo "FOUR_BACKEND_LUA=FAIL target=$target source=$file stage=MiniC_crash exit=$status" >&2
                sed -n '1,28p' "$work/$target/$stem.minic.err" >&2
                failed=$((failed + 1))
                break
            fi
            target_blocked=$((target_blocked + 1))
            grep 'MINIC_BACKEND_REJECT' "$work/$target/$stem.minic.err" \
                >>"$work/$target/rejects.log" || true
            first=$(grep -m 1 'MINIC_BACKEND_REJECT\|error:' \
                "$work/$target/$stem.minic.err" || true)
            echo "FOUR_BACKEND_LUA_TU=BLOCKED target=$target source=$file stage=MiniC $first"
            case "$first" in
                *'.i:'*'error: expected type name'*|*'.i:'*'error: unsupported GNU prefix function attribute'*)
                    line=$(printf '%s\n' "$first" | sed -n \
                        's/.*\.i:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p')
                    if [ -n "$line" ]; then
                        case "$parse_context_lines" in
                            *" $line "*) ;;
                            *)
                                parse_context_lines="$parse_context_lines$line "
                                first_context=$((line > 4 ? line - 4 : 1))
                                last_context=$((line + 4))
                                echo "FOUR_BACKEND_LUA_PARSE_CONTEXT target=$target source=$file source_line=$line begin"
                                sed -n "${first_context},${last_context}p" \
                                    "$work/$target/$stem.i" | \
                                    nl -ba -v "$first_context"
                                echo "FOUR_BACKEND_LUA_PARSE_CONTEXT target=$target source=$file end"
                                ;;
                        esac
                    fi
                    ;;
            esac
            grep -m 2 'MINIC_BACKEND_CALL_GATE' \
                "$work/$target/$stem.minic.err" || true
            # Show independent global + function preflight blockers, not
            # only whichever was logged first. Lua VM has both classes.
            grep -m 1 'MINIC_M4_GLOBAL_FRONTIER' \
                "$work/$target/$stem.minic.err" || true
            grep -m 1 'MINIC_BACKEND_PREFLIGHT' \
                "$work/$target/$stem.minic.err" || true
            grep -m 1 'MINIC_M8_FRAME' \
                "$work/$target/$stem.minic.err" || true
            continue
        fi
        compiled=$((compiled + 1))
        if ! "$gcc" -c -x assembler "$work/$target/$stem.s" \
                -o "$work/$target/$stem.o" \
                >"$work/$target/$stem.as.out" 2>"$work/$target/$stem.as.err"; then
            target_blocked=$((target_blocked + 1))
            echo "FOUR_BACKEND_LUA_TU=BLOCKED target=$target source=$file stage=GNU_AS"
            sed -n '1,9p' "$work/$target/$stem.as.err"
            # Show exact out-of-range AArch64 instructions.
            grep -m 8 -E '\.s:[0-9]+: Error:' "$work/$target/$stem.as.err" |
                while IFS= read -r diagnostic; do
                    line=$(printf '%s\n' "$diagnostic" |
                        sed -n 's/.*\.s:\([0-9][0-9]*\):.*/\1/p')
                    if [ -n "$line" ]; then
                        begin=$((line > 2 ? line - 2 : 1))
                        end=$((line + 2))
                        echo "FOUR_BACKEND_LUA_AS_CONTEXT target=$target source=$file line=$line"
                        sed -n "${begin},${end}p" "$work/$target/$stem.s"
                    fi
                done
            continue
        fi
        assembled=$((assembled + 1))
        echo "FOUR_BACKEND_LUA_TU=PASS target=$target source=$file"
    done
    echo "FOUR_BACKEND_LUA_TU_SUMMARY target=$target total=34 compiled=$compiled assembled=$assembled blocked=$target_blocked"
    if [ -s "$work/$target/rejects.log" ]; then
        awk '
          /MINIC_BACKEND_REJECT/ {
             key=""; for(i=1;i<=NF;i++) if($i ~ /^(stage|kind|backend_line)=/) key=key " " $i;
             if(key!="") count[key]++
          }
          END {for(k in count) printf "%06d%s\n", count[k], k}
        ' "$work/$target/rejects.log" | sort -rn | head -n 30 |
            while IFS= read -r row; do
                echo "FOUR_BACKEND_LUA_ERROR_CLASS target=$target $row"
            done
    fi
    if [ "$failed" -ne 0 ]; then break; fi
    if [ "$target_blocked" -ne 0 ]; then
        echo "FOUR_BACKEND_LUA=BLOCKED target=$target TUs=34 compiled=$compiled assembled=$assembled blocked=$target_blocked"
        blocked=$((blocked+1))
        continue
    fi
    # Real original interpreter with no GCC-compiled Lua objects.
    set --
    for file in $sources; do
        [ "$file" = luac.c ] && continue
        stem=${file%.c}
        set -- "$@" "$work/$target/$stem.o"
    done
    if ! "$gcc" -static "$@" -lm -ldl -o "$work/$target/lua-minic" \
            >"$work/$target/link.out" 2>"$work/$target/link.err"; then
        echo "FOUR_BACKEND_LUA=FAIL target=$target stage=GNU_link" >&2
        sed -n '1,35p' "$work/$target/link.err" >&2
        failed=$((failed+1)); continue
    fi
    if ! timeout 60s "$qemu" "$work/$target/lua-minic" -e "$script" \
            >"$work/$target/mini.out" 2>"$work/$target/mini.err" ||
       ! cmp -s "$work/$target/gnu.out" "$work/$target/mini.out" ||
       ! cmp -s "$work/$target/gnu.err" "$work/$target/mini.err"; then
        echo "FOUR_BACKEND_LUA=FAIL target=$target stage=interpreter_differential" >&2
        diff -u "$work/$target/gnu.out" "$work/$target/mini.out" || true
        sed -n '1,25p' "$work/$target/mini.err" >&2
        failed=$((failed+1)); continue
    fi
    # Real luac.c, MiniC-only objects, GNU entry/CRT/linker.
    set --
    for file in $sources; do
        [ "$file" = lua.c ] && continue
        stem=${file%.c}
        set -- "$@" "$work/$target/$stem.o"
    done
    if ! "$gcc" -static "$@" -lm -ldl -o "$work/$target/luac-minic"; then
        echo "FOUR_BACKEND_LUA=FAIL target=$target stage=luac_link" >&2
        failed=$((failed+1)); continue
    fi
    # A successful luac link does NOT establish a functioning compiler.
    # Compile the same original source with GNU for a byte-exact baseline,
    # then verify that the MiniC-owned luac creates identical bytecode
    # and that both bytecode files execute through the MiniC interpreter.
    set --
    for file in $sources; do
        [ "$file" = lua.c ] && continue
        set -- "$@" "$src/$file"
    done
    if ! "$gcc" -O0 -std=c99 -static -DLUA_USE_LINUX -I"$src" \
            "$@" -lm -ldl -o "$work/$target/luac-gnu" \
            >"$work/$target/luac-gnu-build.out" \
            2>"$work/$target/luac-gnu-build.err"; then
        echo "FOUR_BACKEND_LUA_ORACLE=FAIL target=$target stage=GNU_luac_link" >&2
        sed -n '1,35p' "$work/$target/luac-gnu-build.err" >&2
        exit 1
    fi
    cat >"$work/$target/pressure.lua" <<'LUA'
local record = setmetatable({x=13}, {
    __index=function(_,k) if k == "y" then return 29 end end
})
local function f(n) return n + record.y end
assert(f(record.x) == 42)
local t = {10,3,2,6}
table.sort(t)
assert(table.concat(t, ",") == "2,3,6,10")
local co = coroutine.create(function()
    coroutine.yield(21)
    return 42
end)
local ok,v = coroutine.resume(co)
assert(ok and v == 21)
ok,v = coroutine.resume(co)
assert(ok and v == 42)
print("LUAC_CROSS", f(13), t[1])
LUA
    if ! timeout 60s "$qemu" "$work/$target/luac-gnu" \
            -o "$work/$target/oracle.luac" "$work/$target/pressure.lua" \
            >"$work/$target/luac-gnu.out" 2>"$work/$target/luac-gnu.err"; then
        echo "FOUR_BACKEND_LUA_ORACLE=FAIL target=$target stage=GNU_luac_run" >&2
        sed -n '1,25p' "$work/$target/luac-gnu.err" >&2
        exit 1
    fi
    if ! timeout 60s "$qemu" "$work/$target/luac-minic" \
            -o "$work/$target/mini.luac" "$work/$target/pressure.lua" \
            >"$work/$target/luac-mini.out" 2>"$work/$target/luac-mini.err" ||
       ! cmp -s "$work/$target/oracle.luac" "$work/$target/mini.luac"; then
        echo "FOUR_BACKEND_LUA=FAIL target=$target stage=luac_bytecode_differential" >&2
        sed -n '1,35p' "$work/$target/luac-mini.err" >&2
        failed=$((failed+1)); continue
    fi
    if ! timeout 60s "$qemu" "$work/$target/lua-gnu" \
            "$work/$target/oracle.luac" \
            >"$work/$target/oracle-bytecode.out" \
            2>"$work/$target/oracle-bytecode.err"; then
        echo "FOUR_BACKEND_LUA_ORACLE=FAIL target=$target stage=GNU_bytecode_run" >&2
        exit 1
    fi
    if ! timeout 60s "$qemu" "$work/$target/lua-minic" \
            "$work/$target/mini.luac" \
            >"$work/$target/mini-bytecode.out" \
            2>"$work/$target/mini-bytecode.err" ||
       ! cmp -s "$work/$target/oracle-bytecode.out" "$work/$target/mini-bytecode.out" ||
       ! cmp -s "$work/$target/oracle-bytecode.err" "$work/$target/mini-bytecode.err"; then
        echo "FOUR_BACKEND_LUA=FAIL target=$target stage=luac_runtime_differential" >&2
        diff -u "$work/$target/oracle-bytecode.out" "$work/$target/mini-bytecode.out" || true
        sed -n '1,35p' "$work/$target/mini-bytecode.err" >&2
        failed=$((failed+1)); continue
    fi
    echo "FOUR_BACKEND_LUA_LUAC=PASS target=$target original=luac.c MiniC_objects=33 GNU_QEMU_byte_exact=1 runtime=PASS"
    echo "FOUR_BACKEND_LUA=PASS target=$target original=5.5.0 TUs=34 lua=GNU_QEMU_byte_exact luac=GNU_QEMU_byte_exact"
    passed=$((passed+1))
done
echo "FOUR_BACKEND_LUA_FRONTIER=COMPLETE architectures=2 verified_runtime=$passed blocked=$blocked failed=$failed"
if [ "$failed" -ne 0 ]; then exit 1; fi
if [ "${FOUR_BACKEND_LUA_REQUIRE:-0}" = 1 ]; then
    if [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; then
        echo "FOUR_BACKEND_LUA_ACCEPT=FAIL passed=$passed blocked=$blocked" >&2
        exit 1
    fi
    echo 'FOUR_BACKEND_LUA_ACCEPT=PASS targets=2 original_lua=1 GNU_QEMU=1'
fi

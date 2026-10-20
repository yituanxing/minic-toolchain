#!/bin/sh
# Two-ISA SQLite 3.54.1 original amalgamation frontier.
# Same official sqlite3.c GNU-preprocessed input for MiniC and GNU. NEVER
# interpret a rejected/timeout TU as an accepted SQLite build. The complete
# GNU/QEMU SQL differential is strict when each MiniC object is available.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc binary}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
release=3.54.1
source_id=3540100
expected_sha3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
        "https://www.sqlite.org/2026/sqlite-amalgamation-$source_id.zip" \
        -o "$work/upstream.zip"; then
    echo "FOUR_BACKEND_SQLITE_ORACLE=FAIL stage=download" >&2
    exit 1
fi
python3 - "$work/upstream.zip" "$expected_sha3" <<'PY'
import hashlib, sys
with open(sys.argv[1], "rb") as f:
    actual = hashlib.file_digest(f, "sha3_256").hexdigest()
if actual != sys.argv[2]:
    raise SystemExit("FOUR_BACKEND_SQLITE_ORACLE=FAIL sha3_256=" + actual)
print("FOUR_BACKEND_SQLITE_SOURCE=VERIFIED release=3.54.1 sha3_256=" + actual)
PY
unzip -q "$work/upstream.zip" -d "$work"
src="$work/sqlite-amalgamation-$source_id"
test -s "$src/sqlite3.c" && test -s "$src/sqlite3.h"
cat > "$work/workload.c" <<'SRC'
#define _GNU_SOURCE
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__loongarch__)
#include <signal.h>
#include <ucontext.h>
/* GNU-built test harness only; never injected into SQLite or MiniC objects.
 * Capture the exact faulting PC and ABI argument state without huge traces. */
static void sql_fault(int signum, siginfo_t *info, void *context) {
    const ucontext_t *uc = (const ucontext_t *)context;
    fprintf(stderr,
            "SQLITE_RUNTIME_FAULT signal=%d pc=0x%llx ra=0x%llx"
            " sp=0x%llx a0=0x%llx a1=0x%llx a2=0x%llx"
            " address=%p\n", signum,
            (unsigned long long)uc->uc_mcontext.__pc,
            (unsigned long long)uc->uc_mcontext.__gregs[1],
            (unsigned long long)uc->uc_mcontext.__gregs[3],
            (unsigned long long)uc->uc_mcontext.__gregs[4],
            (unsigned long long)uc->uc_mcontext.__gregs[5],
            (unsigned long long)uc->uc_mcontext.__gregs[6],
            info->si_addr);
    _Exit(98);
}
static void install_sql_fault_handler(void) {
    struct sigaction action = {0};
    action.sa_sigaction = sql_fault;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    (void)sigaction(SIGSEGV, &action, NULL);
    (void)sigaction(SIGBUS, &action, NULL);
}
#else
static void install_sql_fault_handler(void) {}
#endif
static int result(void *data, int n, char **values, char **names) {
    int i;
    (void)data;
    for (i = 0; i < n; ++i)
        printf("%s=%s%s", names[i], values[i] ? values[i] : "NULL",
               i + 1 == n ? "\n" : "|");
    return 0;
}
static void run(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, result, NULL, &error);
    if (rc != SQLITE_OK) {
        /* GNU-built driver interrogates the REAL MiniC sqlite3.o through
         * public APIs. Byte offset + SQL bytes distinguish an internal
         * tokenizer error from a caller/ABI corrupted input pointer. */
        int offset = sqlite3_error_offset(db);
        fprintf(stderr, "sqlite failure rc=%d offset=%d sql=%s error=%s\n",
                rc, offset, sql, error ? error : "");
        if (offset >= 0 && (unsigned long)offset < strlen(sql))
            fprintf(stderr,
                    "SQLITE_RUNTIME_BAD_TOKEN byte=%02x before=%02x after=%02x\n",
                    (unsigned char)sql[offset],
                    offset > 0 ? (unsigned char)sql[offset - 1] : 0,
                    (unsigned char)sql[offset + 1]);
        sqlite3_free(error);
        exit(3);
    }
}
int main(void) {
    sqlite3 *db = NULL;
    install_sql_fault_handler();
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=open\n");
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=schema_and_insert\n");
    /* Preserve each original statement and its execution order while
     * reporting the exact failing phase rather than one large sqlite_exec. */
    fprintf(stderr, "SQLITE_RUNTIME_SUBSTAGE=create_table\n");
    run(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, x INT);");
    fprintf(stderr, "SQLITE_RUNTIME_SUBSTAGE=insert\n");
    run(db, "INSERT INTO t VALUES(1,11),(2,13);");
    fprintf(stderr, "SQLITE_RUNTIME_SUBSTAGE=create_index\n");
    run(db, "CREATE INDEX t_x ON t(x);");
    fprintf(stderr, "SQLITE_RUNTIME_SUBSTAGE=select_sum\n");
    run(db, "SELECT SUM(x) AS sum FROM t;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=transaction\n");
    run(db, "BEGIN; UPDATE t SET x=99 WHERE id=1; ROLLBACK;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=select\n");
    run(db, "SELECT x FROM t WHERE id=1;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=integrity\n");
    run(db, "PRAGMA integrity_check;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=version_and_close\n");
    printf("sqlite-version=%s\n", sqlite3_libversion());
    return sqlite3_close(db) == SQLITE_OK ? 0 : 2;
}
SRC
# Tokenization A/B uses the unmodified SQLite engine, including its lexer
# tables, so one test evaluates all symbol/data/bitwidth decisions at once.
cat > "$work/token-ab.c" <<'SRC'
#include <stdio.h>
extern int sqlite3GetToken(const unsigned char *, int *);
extern const unsigned char sqlite3CtypeMap[256];
int main(void) {
    const char *queries[] = {
        "CREATE TABLE t(id INTEGER PRIMARY KEY, x INT);",
        "#1", "rowid=#1", "SELECT 42, #17, :a_2;",
        "SELECT a FROM t WHERE b=?12 AND c='123';"
    };
    /* Check the entire character-class table, not only a single SQL query.
     * sqlite3CtypeMap['1'] must contain the identifier/digit class bits.
     * The executable and test driver are otherwise unchanged. */
    for (int b = 0; b < 256; ++b)
        printf("ctype:%03d:%02x\n", b, sqlite3CtypeMap[b]);
    for (unsigned long q = 0; q < sizeof(queries)/sizeof(queries[0]); ++q) {
        const unsigned char *text = (const unsigned char *)queries[q];
        unsigned long i = 0U;
        printf("query=%lu\n", q);
        while (text[i] != 0U) {
            int type = 0;
            int width = sqlite3GetToken(text + i, &type);
            printf("%lu:%d:%d:", i, width, type);
            if (width < 1 || width > 100) return 3;
            for (int j = 0; j < width; ++j)
                printf("%02x", text[i + (unsigned long)j]);
            putchar('\n');
            i += (unsigned long)width;
        }
    }
    return 0;
}
SRC
# Independently exercise SQLite's own variadic formatting on the original
# GNU/MiniC objects. This is a low-cost A/B before the full SQL parser:
# no generated SQL, parser instrumentation or code changes are required.
cat > "$work/format-ab.c" <<'SRC'
#include <stdio.h>
extern char *sqlite3_mprintf(const char *, ...);
extern void sqlite3_free(void *);
static void show(const char *label, char *s) {
    printf("%s=%s\\n", label, s ? s : "<null>");
    sqlite3_free(s);
}
int main(void) {
    show("int_positive", sqlite3_mprintf("rowid=#%d", 1));
    show("int_negative", sqlite3_mprintf("rowid=#%d", -1));
    show("long_positive", sqlite3_mprintf("rowid=#%lld", 1LL));
    show("long_negative", sqlite3_mprintf("rowid=#%lld", -1LL));
    show("mix", sqlite3_mprintf("a=%d b=%d c=%lld d=%s", 1, 2, 3LL, "done"));
    return 0;
}
SRC
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
    input="$work/$target/sqlite3.i"
    asm="$work/$target/sqlite3.s"
    # SQLite officially provides a portable 64-bit implementation of
    # multiply128/multiply160 when SQLITE_DISABLE_INTRINSIC is enabled.
    # MiniC does not yet claim 128-bit scalar SSA/ABI correctness, so use
    # SQLite's unmodified supported portable source path for BOTH MiniC
    # and the GNU oracle. This is not a claim of general __uint128_t support.
    define='-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DISABLE_INTRINSIC'
    # Cross-preprocess each target's real GNU libc. Never feed x86 headers.
    if ! "$gcc" -E -P -std=gnu11 $define -I"$src" "$src/sqlite3.c" \
            -o "$input" >"$work/$target/pp.out" 2>"$work/$target/pp.err"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_preprocess" >&2
        sed -n '1,18p' "$work/$target/pp.err" >&2
        failed=$((failed + 1))
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_INPUT target=$target version=$release preprocessed_lines=$(wc -l < "$input") preprocessed_bytes=$(wc -c < "$input")"
    set +e
    /usr/bin/time -f 'MINIC_SQLITE_SECONDS=%e MINIC_SQLITE_MAXRSS_KB=%M' \
        timeout 120s env MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 \
        "$MINIC_CC" --target="$target-linux" -S "$input" -o "$asm" \
        >"$work/$target/mini.out" 2>"$work/$target/mini.err"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        if [ "$status" -ge 128 ] && [ "$status" -ne 124 ]; then
            echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=MiniC_crash status=$status" >&2
            failed=$((failed + 1))
        else
            echo "FOUR_BACKEND_SQLITE=BLOCKED target=$target stage=MiniC status=$status"
            blocked=$((blocked + 1))
        fi
        # One full SQLite compilation collects ALL rejected functions.
        # Group the reject census by stage/opcode and print representative
        # function names, rather than one CI cycle per individual rejection.
        echo "FOUR_BACKEND_SQLITE_FIRST_FAILURE target=$target"
        grep -m 2 -E 'MINIC_BACKEND_REJECT|MINIC_M4_GLOBAL_FRONTIER|MINIC_BACKEND_PREFLIGHT|error:' \
            "$work/$target/mini.err" || true
        awk '
            /MINIC_BACKEND_REJECT/ {
                stage="none"; opcode="none"; function_name="none";
                for (i=1;i<=NF;i++) {
                    if ($i ~ /^stage=/) stage=$i;
                    if ($i ~ /^kind=/) opcode=$i;
                    if ($i ~ /^function=/) function_name=$i;
                }
                key=stage " " opcode
                count[key]++
                if (!(key in example)) example[key]=function_name
            }
            END {
                for (key in count)
                    printf "%06d %s example=%s\n",count[key],key,example[key]
            }
        ' "$work/$target/mini.err" | sort -rn | head -n 20 |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_ERROR_CLASS target=$target $line"
            done
        grep -m 12 'MINIC_M4_GLOBAL_FRONTIER' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_GLOBAL_CLASS target=$target $line"
            done
        grep -m 12 'MINIC_SQLITE_CALL_ARGUMENT' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CALL_ARGUMENT target=$target $line"
            done
        grep -m 12 'MINIC_SQLITE_CONVERSION_LIMIT' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CONVERSION_CLASS target=$target $line"
            done
        grep -m 12 'MINIC_BACKEND_CALL_GATE' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CALL_CLASS target=$target $line"
            done

        tail -n 5 "$work/$target/mini.err"
        lineno=$(sed -n 's/.*sqlite3\.i:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p' \
                 "$work/$target/mini.err" | head -n 1)
        if [ -n "$lineno" ]; then
            start=$((lineno > 4 ? lineno - 4 : 1))
            end=$((lineno + 4))
            echo "FOUR_BACKEND_SQLITE_CONTEXT target=$target input_lines=$start-$end"
            sed -n "${start},${end}p" "$input" |
                nl -ba -v "$start"
        fi
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_COMPILE=PASS target=$target source=sqlite3.c"
    if ! "$gcc" -c -x assembler "$asm" -o "$work/$target/mini.o" \
            >"$work/$target/as.out" 2>"$work/$target/as.err"; then
        echo "FOUR_BACKEND_SQLITE=BLOCKED target=$target stage=GNU_AS"
        sed -n '1,14p' "$work/$target/as.err"
        blocked=$((blocked + 1))
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_ASSEMBLE=PASS target=$target source=sqlite3.c"
    if ! "$gcc" -O0 -std=gnu11 -c -x c "$input" -o "$work/$target/gnu.o" ||
       ! "$gcc" -static -I"$src" "$work/workload.c" "$work/$target/gnu.o" \
             -lm -ldl -lpthread -o "$work/$target/gnu" ||
       ! "$gcc" -static -I"$src" "$work/workload.c" "$work/$target/mini.o" \
             -lm -ldl -lpthread -o "$work/$target/mini"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_or_MiniC_link" >&2
        failed=$((failed + 1))
        continue
    fi
    # First run an independent lexer/token-table oracle to separate raw
    # character recognition errors from parser-reduction/ABI errors.
    token_build_ok=1
    for cc in gnu mini; do
        # SQLite declares its internal lexer static. On COPIES of the
        # original GNU/MiniC objects, expose precisely that one symbol
        # for an independent harness without editing original sqlite3.c.
        objcopy_tool=$("$gcc" -print-prog-name=objcopy)
        if ! "$objcopy_tool" --globalize-symbol=sqlite3GetToken \
                 --globalize-symbol=sqlite3CtypeMap \
                "$work/$target/$cc.o" "$work/$target/token-$cc.o" ||
           ! "$gcc" -static "$work/token-ab.c" "$work/$target/token-$cc.o" \
                -lm -ldl -lpthread -o "$work/$target/token-$cc"; then
            echo "FOUR_BACKEND_SQLITE_LEXER=SKIP target=$target compiler=$cc stage=link"
            token_build_ok=0
            break
        fi
        if ! timeout 45s "$qemu" "$work/$target/token-$cc" \
                >"$work/$target/token-$cc.out" 2>"$work/$target/token-$cc.err"; then
            echo "FOUR_BACKEND_SQLITE_LEXER=FAIL target=$target compiler=$cc stage=QEMU"
            token_build_ok=0
            break
        fi
    done
    if [ "$token_build_ok" -eq 1 ]; then
        if cmp -s "$work/$target/token-gnu.out" "$work/$target/token-mini.out"; then
            echo "FOUR_BACKEND_SQLITE_LEXER=PASS target=$target source_token_matrix=5 ctype_table=256 GNU_QEMU_byte_exact=1"
        else
            echo "FOUR_BACKEND_SQLITE_LEXER=FAIL target=$target stage=GNU_QEMU_differential"
            diff -u "$work/$target/token-gnu.out" "$work/$target/token-mini.out" |
                head -n 35 || true
        fi
    fi
    # Check SQLite's vararg formatter before blaming the lexer/parser.
    if [ "$target" = loongarch64 ]; then
        format_ab_ok=1
        for cc in gnu mini; do
            if ! "$gcc" -static "$work/format-ab.c" "$work/$target/$cc.o" \\
                    -lm -ldl -lpthread -o "$work/$target/format-$cc" ||
               ! timeout 30s "$qemu" "$work/$target/format-$cc" \\
                    >"$work/$target/format-$cc.out" \\
                    2>"$work/$target/format-$cc.err"; then
                format_ab_ok=0
                echo "SQLITE_FORMAT_AB=SKIP builder=$cc stage=link_or_qemu"
                break
            fi
        done
        if [ "$format_ab_ok" -eq 1 ]; then
            if cmp -s "$work/$target/format-gnu.out" "$work/$target/format-mini.out"; then
                echo "SQLITE_FORMAT_AB=PASS target=$target GNU_QEMU_byte_exact=1"
            else
                echo "SQLITE_FORMAT_AB=FAIL target=$target stage=GNU_QEMU_differential"
                diff -u "$work/$target/format-gnu.out" \\
                        "$work/$target/format-mini.out" | head -n 40 || true
            fi
        fi
    fi
    runtime_ok=1
    for cc in gnu mini; do
        if ! timeout 60s "$qemu" "$work/$target/$cc" \
                >"$work/$target/$cc.stdout" 2>"$work/$target/$cc.stderr"; then
            echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=$cc-QEMU" >&2
            sed -n '1,30p' "$work/$target/$cc.stderr" >&2
            if [ "$cc" = mini ] && [ "$target" = loongarch64 ]; then
                # Recompile a disposable instrumented COPY of the exact
                # original preprocessed amalgamation, once with GNU and once
                # with MiniC. Each sqlite3RunParser token is recorded with
                # its length, classification and input bytes. This is an
                # A/B diagnostic only: acceptance still uses ORIGINAL bytes.
                if python3 - "$input" "$work/$target/sqlite-diag.i" <<'PY'
import pathlib, re, sys
src = pathlib.Path(sys.argv[1]).read_text()
rx = r'\bn\s*=\s*sqlite3GetToken\s*\(\s*\(u8\s*\*\)\s*zSql\s*,\s*&tokenType\s*\)\s*;'
matches = list(re.finditer(rx, src))
if len(matches) == 0:
    print("SQLITE_PARSER_AB_INSTRUMENT=SKIP occurrences=" + str(len(matches)))
    raise SystemExit(1)
new = (matches[0].group(0) +
       '\nfprintf(stderr,"SQLITE_PARSER_TOKEN n=%lld type=%d b0=%02x b1=%02x\\n",'
       '(long long)n,tokenType,(unsigned char)zSql[0],'
       '(unsigned char)zSql[1]);')
src = re.sub(rx, lambda _: new, src)
pathlib.Path(sys.argv[2]).write_text(src)
print("SQLITE_PARSER_AB_INSTRUMENT=PASS matched=" + str(len(matches)))
PY
                then
                    parser_ab_ok=1
                    if ! "$gcc" -O0 -c -x c \
                            "$work/$target/sqlite-diag.i" \
                            -o "$work/$target/gnu-diag.o" \
                            >"$work/$target/gnu-diag.compile.out" 2>"$work/$target/gnu-diag.compile.err" ||
                       ! timeout 120s "$MINIC_CC" --target="$target-linux" \
                            -S "$work/$target/sqlite-diag.i" \
                            -o "$work/$target/mini-diag.s" \
                            >"$work/$target/mini-diag.compile.out" 2>"$work/$target/mini-diag.compile.err" ||
                       ! "$gcc" -c -x assembler "$work/$target/mini-diag.s" \
                            -o "$work/$target/mini-diag.o"; then
                        parser_ab_ok=0
                        echo "SQLITE_PARSER_AB=SKIP stage=compile"
                        tail -n 4 "$work/$target/mini-diag.compile.err" || true
                    fi
                    if [ "$parser_ab_ok" -eq 1 ]; then
                        for builder in gnu mini; do
                            if ! "$gcc" -static -I"$src" "$work/workload.c" \
                                    "$work/$target/$builder-diag.o" \
                                    -lm -ldl -lpthread -o "$work/$target/$builder-diag"; then
                                parser_ab_ok=0
                                echo "SQLITE_PARSER_AB=SKIP builder=$builder stage=link"
                                break
                            fi
                            set +e
                            timeout 30s "$qemu" "$work/$target/$builder-diag" \
                                 >"$work/$target/$builder-diag.out" \
                                 2>"$work/$target/$builder-diag.err"
                            ab_status=$?
                            set -e
                            echo "SQLITE_PARSER_AB_RUN builder=$builder status=$ab_status"
                        done
                    fi
                    if [ "$parser_ab_ok" -eq 1 ]; then
                        grep '^SQLITE_PARSER_TOKEN' "$work/$target/gnu-diag.err" \
                             >"$work/$target/gnu-tokens.txt" || true
                        grep '^SQLITE_PARSER_TOKEN' "$work/$target/mini-diag.err" \
                             >"$work/$target/mini-tokens.txt" || true
                        echo "SQLITE_PARSER_AB_COUNTS gnu=$(wc -l < "$work/$target/gnu-tokens.txt") mini=$(wc -l < "$work/$target/mini-tokens.txt")"
                        diff -u "$work/$target/gnu-tokens.txt" \
                                "$work/$target/mini-tokens.txt" |
                            head -n 60 || true
                        tail -n 8 "$work/$target/mini-diag.err"
                    fi
                fi
                # Run the same REAL MiniC-built SQLite binary under QEMU's
                # block trace. This is a diagnostic replay, not a pass.
                set +e
                timeout 20s "$qemu" -d in_asm,exec \
                    -D "$work/$target/mini-qemu.log" \
                    "$work/$target/mini" >"$work/$target/trace.out" \
                    2>"$work/$target/trace.err"
                qemu_trace_status=$?
                set -e
                echo "FOUR_BACKEND_SQLITE_TRACE target=$target replay_status=$qemu_trace_status"
                tail -n 32 "$work/$target/mini-qemu.log" 2>/dev/null |
                    while IFS= read -r line; do
                        echo "FOUR_BACKEND_SQLITE_TRACE_LINE $line"
                    done
                # GNU's function symbol table can map QEMU guest PCs to
                # the exact function responsible, even for 2500+ symbols.
                "$gcc" -print-prog-name=nm >"$work/$target/nm-command"
                nm_tool=$(cat "$work/$target/nm-command")
                "$nm_tool" -n "$work/$target/mini" |
                    awk '$2=="t" || $2=="T" {print $1, $3}' \
                    >"$work/$target/symbols.txt"
                python3 - "$work/$target/mini-qemu.log" \
                        "$work/$target/symbols.txt" <<'PY'
import bisect, re, sys
from collections import deque

pcs = deque(maxlen=12)
branches = deque(maxlen=10)
traceblocks = deque(maxlen=8)
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        m = re.match(r'^0x([0-9a-fA-F]+):', line)
        if m:
            pcs.append(int(m.group(1), 16))
        b = re.search(r'\b(?:bl|b|jirl)\s+[^#\n]*#\s*0x([0-9a-fA-F]+)', line)
        if b:
            branches.append(int(b.group(1), 16))
        t = re.search(r'^Trace .*?\[([0-9a-fA-F]{8,})/', line)
        if t:
            traceblocks.append(int(t.group(1), 16))
symbols = []
with open(sys.argv[2]) as f:
    for line in f:
        v = line.split(maxsplit=1)
        if len(v) == 2:
            try: symbols.append((int(v[0], 16), v[1].strip()))
            except ValueError: pass
symbols.sort()
addresses = [p for p, _ in symbols]
def describe(tag, pc):
    at = bisect.bisect_right(addresses, pc) - 1
    name = symbols[at][1] if at >= 0 else '<unknown>'
    base = symbols[at][0] if at >= 0 else 0
    print(f"FOUR_BACKEND_SQLITE_TRACE_{tag} pc=0x{pc:x} function={name} offset=0x{pc-base:x}")
for pc in pcs:
    describe("PC", pc)
for pc in branches:
    describe("BRANCH", pc)
for pc in traceblocks:
    describe("EXEC", pc)
PY
            fi
            runtime_ok=0
        fi
    done
    if [ "$runtime_ok" -ne 1 ] ||
       ! cmp -s "$work/$target/gnu.stdout" "$work/$target/mini.stdout" ||
       ! cmp -s "$work/$target/gnu.stderr" "$work/$target/mini.stderr"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_QEMU_differential" >&2
        diff -u "$work/$target/gnu.stdout" "$work/$target/mini.stdout" || true
        failed=$((failed + 1))
        continue
    fi
    grep -F "sqlite-version=$release" "$work/$target/mini.stdout" >/dev/null
    echo "FOUR_BACKEND_SQLITE=PASS target=$target source=original_sqlite3.c GNU_QEMU_byte_exact=1"
    passed=$((passed + 1))
done
echo "FOUR_BACKEND_SQLITE_FRONTIER=COMPLETE architectures=2 passed=$passed blocked=$blocked failed=$failed"
if [ "$failed" -ne 0 ]; then exit 1; fi
if [ "${FOUR_BACKEND_SQLITE_REQUIRE:-0}" = 1 ] && { [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; }; then
    echo "FOUR_BACKEND_SQLITE_ACCEPT=FAIL passed=$passed blocked=$blocked" >&2
    exit 1
fi
if [ "$passed" -eq 2 ]; then
    echo "FOUR_BACKEND_SQLITE_ACCEPT=PASS targets=2 original_sqlite3=1 GNU_QEMU=1"
fi

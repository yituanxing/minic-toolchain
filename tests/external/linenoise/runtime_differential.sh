#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/linenoise-runtime"}
assembly=${LINENOISE_ASSEMBLY:-"$root/build/linenoise-discovery/linenoise.s"}
vendor="$work/upstream"
archive="$work/linenoise.tar.gz"
minic_binary="$work/linenoise-minic"
gcc_binary="$work/linenoise-gcc"
minic_tty_binary="$work/linenoise-tty-minic"
gcc_tty_binary="$work/linenoise-tty-gcc"
upstream=a473823d74b93eab2ba83480df16ed37617493f2

rm -rf "$work"
mkdir -p "$vendor"

if test ! -f "$assembly"; then
    printf '%s\n' "FAIL external/linenoise-runtime: missing MiniC assembly $assembly" >&2
    exit 1
fi

curl -fsSL "https://github.com/antirez/linenoise/archive/$upstream.tar.gz" -o "$archive"
tar -xzf "$archive" --strip-components=1 -C "$vendor"
test "$(git hash-object "$vendor/linenoise.c")" = 63f23ddaf0e06dea4d2ac04efa084c3ca275ad8c
test "$(git hash-object "$vendor/linenoise.h")" = 735629b78ed2302d407fb3b6c8e56c6ac24bd6b7

cat >"$work/target_abi.c" <<'EOF'
#include <stddef.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

int main(void) {
    printf("TARGET_ABI termios_size=%zu c_iflag=%zu c_oflag=%zu c_cflag=%zu c_lflag=%zu c_cc=%zu NCCS=%d ",
           sizeof(struct termios),
           offsetof(struct termios, c_iflag),
           offsetof(struct termios, c_oflag),
           offsetof(struct termios, c_cflag),
           offsetof(struct termios, c_lflag),
           offsetof(struct termios, c_cc),
           NCCS);
    printf("BRKINT=%lu ICRNL=%lu INPCK=%lu ISTRIP=%lu IXON=%lu OPOST=%lu CS8=%lu ECHO=%lu ICANON=%lu IEXTEN=%lu ISIG=%lu VMIN=%d VTIME=%d TCSAFLUSH=%d ",
           (unsigned long)BRKINT,
           (unsigned long)ICRNL,
           (unsigned long)INPCK,
           (unsigned long)ISTRIP,
           (unsigned long)IXON,
           (unsigned long)OPOST,
           (unsigned long)CS8,
           (unsigned long)ECHO,
           (unsigned long)ICANON,
           (unsigned long)IEXTEN,
           (unsigned long)ISIG,
           VMIN,
           VTIME,
           TCSAFLUSH);
    printf("winsize_size=%zu ws_row=%zu ws_col=%zu TIOCGWINSZ=%lu\n",
           sizeof(struct winsize),
           offsetof(struct winsize, ws_row),
           offsetof(struct winsize, ws_col),
           (unsigned long)TIOCGWINSZ);
    return 0;
}
EOF
"$riscv_cc" -std=gnu11 -O2 -static "$work/target_abi.c" -o "$work/target-abi"
"$qemu" "$work/target-abi" | tee "$work/target-abi.txt"

cat >"$work/runtime.c" <<'EOF'
#include "linenoise.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    char *line;
    char saved[128];
    FILE *stream;
    size_t count;

    if (argc != 2) return 90;
    if (!linenoiseHistorySetMaxLen(4)) return 11;
    if (!linenoiseHistoryAdd("alpha")) return 12;
    if (!linenoiseHistoryAdd("beta")) return 13;
    if (linenoiseHistorySave(argv[1]) != 0) return 14;

    line = linenoise("unused> ");
    if (line == NULL) return 15;
    printf("line=%s\n", line);
    linenoiseFree(line);

    stream = fopen(argv[1], "r");
    if (stream == NULL) return 16;
    count = fread(saved, 1, sizeof(saved) - 1, stream);
    if (ferror(stream)) {
        fclose(stream);
        return 17;
    }
    saved[count] = '\0';
    fclose(stream);
    printf("history=%s", saved);
    return 0;
}
EOF

# First prove that MiniC's emitted assembly is accepted by the real target assembler
# and can link against the real RISC-V glibc. The same GCC-built harness is used for
# both variants so differences are attributable to the linenoise translation unit.
"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$assembly" "$work/runtime.c" -o "$minic_binary"
"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$vendor/linenoise.c" "$work/runtime.c" -o "$gcc_binary"

run_variant() {
    name=$1
    binary=$2
    history=$3

    set +e
    printf 'hello from pipe\n' | "$qemu" "$binary" "$history" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"
    status=$?
    set -e
    printf '%s\n' "$status" >"$work/$name.status"
}

run_variant minic "$minic_binary" "$work/minic.history"
run_variant gcc "$gcc_binary" "$work/gcc.history"

minic_status=$(cat "$work/minic.status")
gcc_status=$(cat "$work/gcc.status")
if test "$gcc_status" -ne 0; then
    printf '%s\n' "FAIL external/linenoise-runtime: GCC reference exit=$gcc_status" >&2
    cat "$work/gcc.stdout" >&2 || true
    cat "$work/gcc.stderr" >&2 || true
    exit 1
fi
if test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' \
        "FAIL external/linenoise-runtime: exit differs minic=$minic_status gcc=$gcc_status" >&2
    cat "$work/minic.stdout" >&2 || true
    cat "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stdout" "$work/gcc.stdout"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: stdout differs from GCC reference' >&2
    diff -u "$work/gcc.stdout" "$work/minic.stdout" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.stderr" "$work/gcc.stderr"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: stderr differs from GCC reference' >&2
    diff -u "$work/gcc.stderr" "$work/minic.stderr" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.history" "$work/gcc.history"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: history file differs from GCC reference' >&2
    diff -u "$work/gcc.history" "$work/minic.history" >&2 || true
    exit 1
fi
if ! grep -Fx 'line=hello from pipe' "$work/minic.stdout" >/dev/null; then
    printf '%s\n' 'FAIL external/linenoise-runtime: non-TTY input path did not return expected line' >&2
    exit 1
fi
printf '%s\n' 'PASS external/linenoise-runtime non-tty=pipe history=save differential=gcc-byte-exact'

cat >"$work/tty_runtime.c" <<'EOF'
#include "linenoise.h"

#include <stdio.h>

int main(void) {
    char *line = linenoise("p> ");
    if (line == NULL) return 20;
    printf("RESULT=%s\n", line);
    linenoiseFree(line);
    return 0;
}
EOF

"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$assembly" "$work/tty_runtime.c" -o "$minic_tty_binary"
"$riscv_cc" -std=gnu11 -O2 -static -I"$vendor" \
    "$vendor/linenoise.c" "$work/tty_runtime.c" -o "$gcc_tty_binary"

cat >"$work/pty_driver.py" <<'EOF'
import errno
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

qemu, binary, output, status_path = sys.argv[1:5]
master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
process = subprocess.Popen(
    [qemu, binary],
    stdin=slave,
    stdout=slave,
    stderr=slave,
    close_fds=True,
)
os.close(slave)
data = bytearray()
sent = False
deadline = time.monotonic() + 8.0

try:
    while True:
        if not sent and b"p> " in data:
            os.write(master, b"hello\r")
            sent = True
        readable, _, _ = select.select([master], [], [], 0.05)
        if readable:
            try:
                chunk = os.read(master, 4096)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    chunk = b""
                else:
                    raise
            if chunk:
                data.extend(chunk)
        if process.poll() is not None:
            while True:
                readable, _, _ = select.select([master], [], [], 0)
                if not readable:
                    break
                try:
                    chunk = os.read(master, 4096)
                except OSError as exc:
                    if exc.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                data.extend(chunk)
            break
        if time.monotonic() >= deadline:
            process.kill()
            process.wait()
            raise SystemExit("PTY_TIMEOUT")
finally:
    os.close(master)

with open(output, "wb") as stream:
    stream.write(data)
with open(status_path, "w", encoding="ascii") as stream:
    stream.write(f"{process.returncode}\n")
if not sent:
    raise SystemExit("PTY_PROMPT_NOT_SEEN")
EOF

python3 "$work/pty_driver.py" "$qemu" "$minic_tty_binary" \
    "$work/minic.tty" "$work/minic.tty.status"
python3 "$work/pty_driver.py" "$qemu" "$gcc_tty_binary" \
    "$work/gcc.tty" "$work/gcc.tty.status"

minic_tty_status=$(cat "$work/minic.tty.status")
gcc_tty_status=$(cat "$work/gcc.tty.status")
if test "$gcc_tty_status" -ne 0; then
    printf '%s\n' "FAIL external/linenoise-runtime: GCC PTY reference exit=$gcc_tty_status" >&2
    cat "$work/gcc.tty" >&2 || true
    exit 1
fi
if test "$minic_tty_status" -ne "$gcc_tty_status"; then
    printf '%s\n' \
        "FAIL external/linenoise-runtime: PTY exit differs minic=$minic_tty_status gcc=$gcc_tty_status" >&2
    cat "$work/minic.tty" >&2 || true
    exit 1
fi
if ! cmp -s "$work/minic.tty" "$work/gcc.tty"; then
    printf '%s\n' 'FAIL external/linenoise-runtime: PTY transcript differs from GCC reference' >&2
    python3 - "$work/gcc.tty" "$work/minic.tty" <<'PY'
import sys
for label, path in (("gcc", sys.argv[1]), ("minic", sys.argv[2])):
    data = open(path, "rb").read()
    print(f"{label}.tty={data!r}", file=sys.stderr)
PY
    exit 1
fi
if ! grep -aF 'RESULT=hello' "$work/minic.tty" >/dev/null; then
    printf '%s\n' 'FAIL external/linenoise-runtime: PTY edit did not return expected line' >&2
    cat "$work/minic.tty" >&2 || true
    exit 1
fi

stdout_bytes=$(wc -c <"$work/minic.stdout" | tr -d ' ')
history_bytes=$(wc -c <"$work/minic.history" | tr -d ' ')
tty_bytes=$(wc -c <"$work/minic.tty" | tr -d ' ')
minic_size=$(wc -c <"$minic_binary" | tr -d ' ')
gcc_size=$(wc -c <"$gcc_binary" | tr -d ' ')
printf '%s\n' \
    "PASS external/linenoise-runtime tty=pty input=hello differential=gcc-byte-exact exit=$minic_tty_status transcript=$tty_bytes"
printf '%s\n' \
    "PASS external/linenoise-runtime upstream=$upstream non_tty=1 tty=1 stdout=$stdout_bytes history_bytes=$history_bytes minic_binary=$minic_size gcc_binary=$gcc_size"

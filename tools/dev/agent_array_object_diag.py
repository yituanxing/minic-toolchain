from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
work = root / "build" / "array-object-diag"
work.mkdir(parents=True, exist_ok=True)
minic = Path(os.environ.get("MINIC", root / "build/array-object-compiler/bin/minic"))
cc = os.environ.get("HOST_CC", "cc")

fixed = """struct CpuMask { unsigned long bits[1]; };
struct FixedArrayHolder { int head; unsigned long values[4]; };
struct FlexibleArrayHolder { int head; unsigned long values[]; };
"""
cases = {
    "fixed_size": "unsigned long f(struct FixedArrayHolder *p){return sizeof(p->values);}",
    "fixed_address": "unsigned long f(struct FixedArrayHolder *p){return sizeof(*(&p->values));}",
    "fixed_typeof": "unsigned long f(struct FixedArrayHolder *p){return sizeof(typeof(p->values));}",
    "fixed_index": "unsigned long f(struct FixedArrayHolder *p){return p->values[2];}",
    "fixed_decay": "unsigned long *f(struct FixedArrayHolder *p){return p->values;}",
    "flex_address": "struct CpuMask *f(struct FlexibleArrayHolder *p){return (struct CpuMask *)&p->values;}",
    "local_address": "unsigned long f(void){unsigned long a[3]; return sizeof(*(&a));}",
    "local_typeof": "unsigned long f(void){unsigned long a[3]; return sizeof(typeof(a));}",
}
failed = False
for name, body in cases.items():
    src = work / f"{name}.c"
    pre = work / f"{name}.i"
    asm = work / f"{name}.s"
    src.write_text(fixed + body + "\n")
    subprocess.run([cc, "-E", "-P", "-std=gnu11", "-x", "c", str(src), "-o", str(pre)], check=True)
    run = subprocess.run([str(minic), "-S", str(pre), "-o", str(asm)], text=True, capture_output=True)
    print(f"ARRAY_DIAG {name} status={run.returncode}")
    if run.stdout:
        print(run.stdout, end="")
    if run.stderr:
        print(run.stderr, end="", file=sys.stderr)
    failed = failed or run.returncode != 0
raise SystemExit(1 if failed else 0)

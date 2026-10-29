#!/usr/bin/env bash
# GNU ELF SHF_LINK_ORDER semantics: a section is identified by its linked-to
# *anchor*, not merely by name or by resolved sh_link SectionID.  GCC emits
# duplicate __patchable_function_entries sections even for one .text.
set -Eeuo pipefail
: "${MINIAS:?absolute or relative minic-as path required}"
for tool in gcc as ld readelf python3; do
    command -v "$tool" >/dev/null
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A real GCC producer catches the original strict Linux 3x7 failure:
# __patchable_function_entries,"awo",@progbits,.LPFE0/.LPFE1.
cat >"$work/patchable.c" <<'C'
int foo(void) { return 1; }
int bar(void) { return 2; }
C
gcc -S -O0 -ffunction-sections -fpatchable-function-entry=2,2 \
    -fno-asynchronous-unwind-tables -fno-stack-protector \
    -o "$work/patchable.s" "$work/patchable.c"
grep -q '__patchable_function_entries,"awo",@progbits,.LPFE0' "$work/patchable.s"
grep -q '__patchable_function_entries,"awo",@progbits,.LPFE1' "$work/patchable.s"
"$MINIAS" --target=x86_64 -o "$work/patchable-mini.o" "$work/patchable.s"
as --64 -o "$work/patchable-gnu.o" "$work/patchable.s"

python3 - "$work/patchable-mini.o" "$work/patchable-gnu.o" <<'PY'
import re, subprocess, sys

def sections(path):
    text=subprocess.check_output(['readelf','-W','-S',path],text=True)
    result={}
    for line in text.splitlines():
        m=re.match(r'^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+'
                   r'[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+'
                   r'[0-9a-f]+\s+([A-Z]+)\s+(\d+)\s+\d+\s+\d+\s*$',line)
        if m:
            number,name,flags,link=m.groups()
            result.setdefault(name,[]).append((int(number),flags,int(link)))
    return result

for path in sys.argv[1:]:
    table=sections(path)
    patches=table.get('__patchable_function_entries',[])
    if len(patches)!=2 or any('WAL' != f for _,f,_ in patches):
        raise SystemExit(f'patchable section identity/flags mismatch: {path}: {patches}')
    expected={table[f'.text.{name}'][0][0] for name in ('foo','bar')}
    actual={sh_link for _,_,sh_link in patches}
    if actual!=expected:
        raise SystemExit(f'patchable sh_link target mismatch: {path}: {actual} != {expected}')
print('MINIAS_LINK_ORDER_GCC=PASS duplicate_same_name=2 target=.text.foo,.text.bar')
PY

for which in mini gnu; do
    ld -r --gc-sections -u foo "$work/patchable-$which.o" \
        -o "$work/patchable-$which-gc.o"
done
python3 - "$work/patchable-mini-gc.o" "$work/patchable-gnu-gc.o" <<'PY'
import re, subprocess, sys
for path in sys.argv[1:]:
    text=subprocess.check_output(['readelf','-W','-S',path],text=True)
    if len(re.findall(r'^\s*\[\s*\d+\]\s+__patchable_function_entries\s',text,re.M))!=1:
        raise SystemExit(f'GNU ld GC failed to discard one link-order dependent: {path}')
    if not re.search(r'^\s*\[\s*\d+\]\s+\.text\.foo\s',text,re.M) or \
       re.search(r'^\s*\[\s*\d+\]\s+\.text\.bar\s',text,re.M):
        raise SystemExit(f'GNU ld GC selected wrong code section: {path}')
print('MINIAS_LINK_ORDER_GC=PASS live_foo_patch=1 dead_bar_patch=0')
PY

# Two different anchors in the *same* linked text section still create two
# separate metadata sections.  Re-selecting the first anchor reopens it.
cat >"$work/same-text.s" <<'ASM'
.section .meta,"ao",@progbits,anchor_one
.long anchor_one
.section .meta,"ao",@progbits,anchor_two
.long anchor_two
.section .meta,"ao",@progbits,anchor_one
.long anchor_one
.text
anchor_one: ret
anchor_two: ret
ASM
"$MINIAS" --target=x86_64 -o "$work/same-text-mini.o" "$work/same-text.s"
as --64 -o "$work/same-text-gnu.o" "$work/same-text.s"
python3 - "$work/same-text-mini.o" "$work/same-text-gnu.o" <<'PY'
import re,subprocess,sys
for path in sys.argv[1:]:
    text=subprocess.check_output(['readelf','-W','-S',path],text=True)
    chunks=re.findall(r'^\s*\[\s*\d+\]\s+\.meta\s+PROGBITS\s+'
                      r'\S+\s+\S+\s+([0-9a-f]+)\s+\S+\s+AL\s+'
                      r'(\d+)\s+\d+\s+\d+',text,re.M)
    if len(chunks)!=2 or sorted(int(x,16) for x,_ in chunks)!=[4,8] or \
       len(set(link for _,link in chunks))!=1:
        raise SystemExit(f'same-target distinct-anchor identity lost: {path}\n{text}')
print('MINIAS_LINK_ORDER_ANCHOR_KEY=PASS two_distinct_anchors_same_sh_link=1 reopen=1')
PY

# Combined `oG` metadata: the linked target and the SHT_GROUP are orthogonal.
cat >"$work/group.s" <<'ASM'
.section .meta,"aoG",@progbits,group_target,group_sig,comdat
.long group_target
.section .text.group_target,"axG",@progbits,group_sig,comdat
group_target: ret
ASM
"$MINIAS" --target=x86_64 -o "$work/group-mini.o" "$work/group.s"
as --64 -o "$work/group-gnu.o" "$work/group.s"
python3 - "$work/group-mini.o" "$work/group-gnu.o" <<'PY'
import re,subprocess,sys
for path in sys.argv[1:]:
    hdr=subprocess.check_output(['readelf','-W','-S',path],text=True)
    group=subprocess.check_output(['readelf','--section-groups',path],text=True)
    target=re.search(r'^\s*\[\s*(\d+)\]\s+\.text\.group_target\s',hdr,re.M)
    meta=re.search(r'^\s*\[\s*\d+\]\s+\.meta\s+PROGBITS\s+'
                   r'\S+\s+\S+\s+\S+\s+\S+\s+ALG\s+(\d+)',hdr,re.M)
    if not target or not meta or target.group(1)!=meta.group(1) or \
       not re.search(r'COMDAT group section.*?\[group_sig\]',group) or \
       not re.search(r'\.meta',group):
        raise SystemExit(f'link-order + COMDAT semantic mismatch: {path}')
print('MINIAS_LINK_ORDER_COMDAT=PASS group_signature=group_sig sh_link=member')
PY

# GNU accepts a missing anchor, absolute symbol, and test-only numeric index
# but they yield sh_link=0.  A nonexistent symbolic anchor is fatal, as is
# an extra argument without `o`, or an otherwise unsupported section flag.
cat >"$work/zero.s" <<'ASM'
.set absolute_anchor,17
.section .meta.a,"ao",@progbits,absolute_anchor
.long 1
.section .meta.b,"ao",@progbits,42
.long 2
.section .meta.c,"ao",@progbits
.long 3
ASM
"$MINIAS" --target=x86_64 -o "$work/zero-mini.o" "$work/zero.s"
as --64 -o "$work/zero-gnu.o" "$work/zero.s"
python3 - "$work/zero-mini.o" "$work/zero-gnu.o" <<'PY'
import re,subprocess,sys
for path in sys.argv[1:]:
    text=subprocess.check_output(['readelf','-W','-S',path],text=True)
    matches=re.findall(r'^\s*\[\s*\d+\]\s+(\.meta\.[abc])\s+PROGBITS\s+'
                       r'\S+\s+\S+\s+\S+\s+\S+\s+AL\s+(\d+)',text,re.M)
    if len(matches)!=3 or {name for name,_ in matches} != \
       {'.meta.a','.meta.b','.meta.c'} or any(int(link)!=0 for _,link in matches):
        raise SystemExit(f'link-order zero sh_link mismatch: {path}: {matches}')
PY
cat >"$work/bad-undefined.s" <<'ASM'
.section .meta,"ao",@progbits,unresolved_target
.long 0
ASM
cat >"$work/bad-extra.s" <<'ASM'
.section .meta,"aw",@progbits,target
.long 0
target:
ASM
cat >"$work/bad-flag.s" <<'ASM'
.section .meta,"az",@progbits
.long 0
ASM
for bad in undefined extra flag; do
    if "$MINIAS" --target=x86_64 -o "$work/bad-mini-$bad.o" \
          "$work/bad-$bad.s" >"$work/bad-mini-$bad.log" 2>&1; then
        echo "MiniAS silently accepted $bad .section" >&2; exit 1
    fi
    if as --64 -o "$work/bad-gnu-$bad.o" \
          "$work/bad-$bad.s" >"$work/bad-gnu-$bad.log" 2>&1; then
        echo "GNU unexpectedly accepted $bad .section" >&2; exit 1
    fi
done
echo 'MINIAS_LINK_ORDER_NEGATIVE=PASS undefined=1 extra=1 unknown_flag=1 zero_cases=3'

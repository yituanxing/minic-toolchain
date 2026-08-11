#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]

path = root / "docs/DEVIATIONS.md"
text = path.read_text()
replacement = '''## DEV-0004: bounded first-class enum representation / 受控的一等 enum 表示\n\n- Status / 状态: Active, substantially narrowed / 活跃，但已大幅收窄。\n- Rule / 规则: Accepted GNU C enum syntax must preserve stable type identity, incomplete/complete lifecycle, exact constant values, and target-compatible integer semantics. 已接受的 GNU C enum 必须保留稳定类型身份、incomplete/complete 生命周期、精确常量值与目标兼容整数语义。\n- Scope / 范围: Program-owned `MinicEnum`/`MinicEnumerator`, `MINIC_TYPE_BASE_ENUM`, enum completion refresh, typed ConstEval, compatibility, DataLayout, and Linux 6.6.143 enum pressure. 涉及 Program-owned enum/enumerator 实体、一等 enum `MinicType`、completion refresh、typed ConstEval、兼容性、DataLayout 与 Linux 6.6.143 enum 压力。\n- Reason / 原因: Foundation EnumEntity v1 now preserves stable `EnumId` identity through forward declarations, typedefs, pointers, functions and AST storage. Enumerators retain typed 64-bit bits and a completed enum caches the GCC-compatible integer representation required by the active RV64 model. The remaining shortcut is that compatible sign/rank is cached into each persisted `MinicType` and refreshed on completion instead of being queried from a canonical future TypeContext/Target integer model on demand. Foundation EnumEntity v1 已经让 forward declaration、typedef、pointer、function 与 AST storage 保留稳定 `EnumId`；enumerator 保存 typed 64-bit bits，completed enum 缓存当前 RV64 下 GCC-compatible 整数表示。剩余简化是 compatible sign/rank 仍缓存进持久化 `MinicType` 并在 completion 时刷新，而不是由未来 canonical TypeContext/Target integer model 按需查询。\n- Risk / 风险: Completion currently performs a bounded Program-wide refresh of persisted enum type copies, and target integer-selection policy is still RV64-oriented. C23 fixed underlying enum syntax, `-fshort-enums`, and enum values beyond the current 64-bit ConstEval range are not represented yet. completion 当前会对 Program 中已持久化的 enum type 副本做受控刷新，整数表示选择策略仍面向 RV64；C23 固定 underlying type、`-fshort-enums` 与超过当前 64-bit ConstEval 范围的 enum value 尚未建模。\n- Exit criteria / 退出条件:\n  1. Move enum compatible-integer selection into the canonical target integer model and remove Program-wide cached-type refresh. 将 enum compatible integer 选择移入 canonical target integer model，并删除 Program-wide cached type refresh。\n  2. Preserve enum identity/representation through the future TypeContext/SymbolTable without parser-linear tag/enumerator lookup. 在未来 TypeContext/SymbolTable 中继续保持 enum identity/representation，并移除 parser-linear tag/enumerator lookup。\n  3. Add C23 fixed-underlying-type / `-fshort-enums` policy only when a real language-mode or workload requires it; until then keep those modes explicitly unsupported. 仅在真实 language mode/workload 施压时加入 C23 fixed underlying type / `-fshort-enums` policy，在此之前保持显式 unsupported。\n- Target milestone / 目标里程碑: Target integer model + TypeContext consolidation before multi-target GNU enum claims / 多 target GNU enum 完整声明前的 Target integer model + TypeContext 收敛。\n- Related evidence / 相关证据: Linux `init/main.i` requires forward incomplete enum identity before completion and reaches `enum mm_cid_state { MM_CID_UNSET = -1U, MM_CID_LAZY_PUT = 1U << 31 }` at line 16618; the same TU later contains 64-bit positive enum values such as `0xffffffffULL << 32`. Linux `init/main.i` 既要求 incomplete enum 在后续 completion 前保持身份，又在 16618 出现 unsigned-range enum，并在后文出现 `0xffffffffULL << 32` 等 64-bit 正值。\n\n'''
pattern = r'(?ms)^## DEV-0004:.*?(?=^## Resolved deviations / 已解决偏离)'
text, count = re.subn(pattern, replacement, text, count=1)
if count != 1:
    raise SystemExit(f"DEV-0004 replacement: expected 1, found {count}")
path.write_text(text)

path = root / "tests/compiler/c0/run-gnu-enum-entity.sh"
text = path.read_text()
anchor = '''grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

printf '%s\\n' 'PASS compiler/c0/gnu_enum_entity program-owned=1 stable-enum-id=1 forward-completion=1 typed-bits=1 uint32=1 ulong64=1 mixed-long=1 compatible-type=1 distinct-enum=1 duplicate=reject'
'''
replacement = '''grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

cat >"$work/incomplete-object.c" <<'EOF'
enum IncompleteObject;
enum IncompleteObject object;
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-object.c" -o "$work/incomplete-object.i"
if "$minic" -S "$work/incomplete-object.i" -o "$work/incomplete-object.s" 2>"$work/incomplete-object.stderr"; then
    printf '%s\\n' 'FAIL compiler/c0/gnu_enum_entity: incomplete enum object storage accepted' >&2
    exit 1
fi

printf '%s\\n' 'PASS compiler/c0/gnu_enum_entity program-owned=1 stable-enum-id=1 forward-completion=1 typed-bits=1 uint32=1 ulong64=1 mixed-long=1 compatible-type=1 distinct-enum=1 incomplete-object=reject duplicate=reject'
'''
if text.count(anchor) != 1:
    raise SystemExit(f"enum runner documentation anchor: expected 1, found {text.count(anchor)}")
path.write_text(text.replace(anchor, replacement, 1))

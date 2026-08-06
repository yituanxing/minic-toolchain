#!/usr/bin/env bash
set -Eeuo pipefail

python3 - <<'PY'
from pathlib import Path

probe = Path('tests/external/cjson/probe.sh')
text = probe.read_text()
old = "    *':1:9: error: expected type name')"
new = "    *':2:16: error: use of undeclared record tag')"
if text.count(old) != 1:
    raise SystemExit(f'old cJSON diagnostic marker count={text.count(old)}')
text = text.replace(old, new, 1)
old = "    'PASS external/cjson frontier=size_t-long-unsigned-int diagnostic=expected-type-name source=cJSON-1.7.19 offline=1'"
new = "    'PASS external/cjson frontier=self-referential-incomplete-record diagnostic=undeclared-record-tag source=cJSON-1.7.19 offline=1'"
if text.count(old) != 1:
    raise SystemExit(f'old cJSON PASS marker count={text.count(old)}')
probe.write_text(text.replace(old, new, 1))

readme = Path('tests/external/cjson/README.md')
text = readme.read_text()
start = text.index('## Current exact frontier / 当前精确前沿\n')
end = text.index('## Validation ladder / 验证阶梯\n')
section = '''## Current exact frontier / 当前精确前沿

The clean-checkout probe preprocesses the unchanged core with a minimal Hosted header surface. `size_t` remains derived from the target compiler's `__SIZE_TYPE__`, so the RV64 form is target-correct:

干净检出探针使用最小 Hosted 头环境预处理未修改核心。`size_t` 继续由目标编译器的 `__SIZE_TYPE__` 派生，因此 RV64 形式保持目标正确：

```c
typedef long unsigned int size_t;
```

MiniC now accepts this declaration with native signed and unsigned LONG rank identities, C integer conversions, eight-byte RV64 layout, and full-width code generation. The next unchanged cJSON source begins its linked object definition:

MiniC 现已通过原生有符号/无符号 LONG Rank、C 整数转换、RV64 八字节布局与全宽代码生成接受该声明。随后未修改的 cJSON 源码开始定义链式对象：

```c
typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
```

The next exact MiniC diagnostic is:

新的精确首条诊断为：

```text
cJSON.i:2:16: error: use of undeclared record tag
```

The active blocker is incomplete record-tag introduction and self-reference: a tagged record must become visible while its definition is still incomplete so pointer members can refer to the same record. This is a category-A cross-project hotspot used by linked lists, trees, graphs, parser nodes, and runtime objects throughout cJSON, Lua, TinyCC, SQLite, musl, and Linux.

当前缺口是不完整结构体标签的引入与自引用：带标签结构体在定义尚未完成时就必须可见，才能让指针成员引用自身。它属于 A 类多项目热点，cJSON、Lua、TinyCC、SQLite、musl 与 Linux 中的链表、树、图、Parser 节点和运行时对象都会反复使用。

`tests/external/cjson/probe.sh` permanently verifies the vendored identities, recreates the target-accurate preprocessing environment without network access, and requires this exact frontier. Crossing it intentionally fails the gate until the next bounded branch records the following real source boundary.

`tests/external/cjson/probe.sh` 永久校验 Vendor 身份，在无网络条件下重建目标正确的预处理环境，并要求该精确前沿。当 MiniC 越过此处时，门禁会主动失败，直到下一条范围受限分支记录后续真实源码边界。

'''
readme.write_text(text[:start] + section + text[end:])
PY

bash .github/scripts/compiler-c0-full-gate.sh

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git rm \
  .github/scripts/finalize-rv64-long-frontier.sh \
  .github/workflows/discover-cjson-after-long.yml \
  .github/workflows/finalize-rv64-long-frontier.yml
git add tests/external/cjson
git commit -m "tests: advance cJSON to self-referential records" -m "Record that target-correct RV64 size_t now crosses the previous cJSON frontier and freeze the next exact diagnostic at incomplete record-tag introduction and self-reference. Verify the complete clean-checkout compiler gate before committing.\n\n中文说明：记录目标正确的 RV64 size_t 已越过旧 cJSON 前沿，并将新的精确诊断冻结在不完整结构体标签引入与自引用；提交前通过完整干净检出编译器门禁。\n\nValidation / 验证： production inventory, formatting, Debug, Release -Werror, ASan/UBSan, RV64/QEMU, 40 GCC/MiniC differential programs, tiny-AES, and updated offline cJSON frontier PASS."
git push origin HEAD:frontend/rv64-long-integers

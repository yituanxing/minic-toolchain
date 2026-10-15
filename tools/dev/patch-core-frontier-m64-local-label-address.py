from pathlib import Path

source_path = Path('tools/dev/patch-core-frontier-m64-local-label-address-original.py')
source = source_path.read_text()
old_anchor = "    anchor = '''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n"
old_repl = "    repl = '''    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n"
if source.count(old_anchor) != 1:
    raise SystemExit(f'M64v2 anchor declaration count={source.count(old_anchor)}')
if source.count(old_repl) != 1:
    raise SystemExit(f'M64v2 replacement declaration count={source.count(old_repl)}')
source = source.replace(old_anchor, "    anchor = r'''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:\n", 1)
source = source.replace(old_repl, "    repl = r'''    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:\n", 1)
exec(compile(source, str(source_path), 'exec'), {'__name__': '__main__'})

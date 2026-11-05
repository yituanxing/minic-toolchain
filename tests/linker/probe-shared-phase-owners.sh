#!/usr/bin/env bash
# The ET_DYN owner split is a pure architecture move.  Keep one SharedImage,
# preserve metadata-before-layout / relocation-after-layout ordering, and
# reuse existing real MiniAS/MiniLD fixture contracts for observable bytes.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
for tool in python3 ar as ld readelf "${HOST_CC:-cc}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_PHASE=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done
for binary in "$MINIAS" "$MINILD"; do
    if [[ ! -x "$binary" ]]; then
        echo "MINILD_SHARED_PHASE=INCONCLUSIVE missing_binary=$binary" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-phase.XXXXXX")
finish() {
    if [[ ${MINILD_SHARED_PHASE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_PHASE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

repo = Path(sys.argv[1])
src = repo / 'linker' / 'src'
files = (
    'linker_core.inc',
    'linker_shared_output.inc',
    'linker_shared_pre_layout.inc',
    'linker_shared_finalize.inc',
    'linker_shared_program_plan.inc',
    'linker_shared_reloc_scan.inc',
    'linker_shared_target_abi.inc',
    'linker_shared_riscv_abi.inc',
    'linker_eh_frame.inc',
    'linker_version.inc',
    'linker_shared_reloc_apply.inc',
    'linker_shared_riscv_target.inc',
)
sources = {}
for name in files:
    path = src / name
    if not path.is_file():
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL missing={name}')
    sources[name] = path.read_text(encoding='utf-8')

makefile = (repo / 'Makefile').read_text(encoding='utf-8')
build_block = re.search(r'^MINILD_SOURCES\s*:=\s*(.*?)^MINILD_OBJECTS\s*:=',
                        makefile, re.M | re.S)
if build_block is None:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL build_sources')
compiled = re.findall(
    r'\b(?:linker/src|tools/minic-ld)/[\w./-]+\.(?:c|inc)\b',
    build_block.group(1),
)
if compiled.count('linker/src/linker.c') != 1 or any(
        path.endswith('.inc') for path in compiled):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL build_translation_units')

edges = {
    ('linker_core.inc', 'linker_shared_output.inc'),
    ('linker_shared_output.inc', 'linker_shared_target_abi.inc'),
    ('linker_shared_target_abi.inc', 'linker_shared_riscv_abi.inc'),
    ('linker_shared_output.inc', 'linker_shared_pre_layout.inc'),
    ('linker_shared_output.inc', 'linker_shared_finalize.inc'),
    ('linker_shared_output.inc', 'linker_shared_program_plan.inc'),
    ('linker_shared_pre_layout.inc', 'linker_shared_reloc_scan.inc'),
    ('linker_shared_pre_layout.inc', 'linker_eh_frame.inc'),
    ('linker_shared_finalize.inc', 'linker_version.inc'),
    ('linker_shared_finalize.inc', 'linker_shared_reloc_apply.inc'),
    ('linker_shared_reloc_apply.inc', 'linker_shared_riscv_target.inc'),
}
targets = set(files) - {'linker_core.inc'}
for name, source in sources.items():
    for target in targets:
        uses = re.findall(
            r'^\s*#\s*include\s+"' + re.escape(target) + r'"\s*$',
            source, re.M,
        )
        want = 1 if (name, target) in edges else 0
        if len(uses) != want:
            raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                             f'include={name}->{target} '
                             f'actual={len(uses)} expected={want}')

driver = sources['linker_shared_output.inc']
pre = sources['linker_shared_pre_layout.inc']
fin = sources['linker_shared_finalize.inc']
prog = sources['linker_shared_program_plan.inc']
if not (driver.index('#include "linker_shared_pre_layout.inc"') <
        driver.index('#include "linker_shared_finalize.inc"') <
        driver.index('#include "linker_shared_program_plan.inc"') <
        driver.index('static bool shared_write_object(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL driver_stage_order')
if not (driver.index('#include "linker_shared_target_abi.inc"') <
        driver.index('#include "linker_shared_pre_layout.inc"')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL target_abi_before_mode')
if not (pre.index('#include "linker_shared_reloc_scan.inc"') <
        pre.index('#include "linker_eh_frame.inc"') <
        pre.index('static bool shared_prepare_metadata(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL reservation_order')
if not (fin.index('static bool shared_finalize_ehdr_symbol(') <
        fin.index('static bool shared_fill_dynsym(') <
        fin.index('#include "linker_version.inc"') <
        fin.index('#include "linker_shared_reloc_apply.inc"') <
        fin.index('static bool shared_fill_dynamic(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL finalization_order')
if not (prog.index('static bool shared_tls_segment(') <
        prog.index('static bool minild_shared_program_plan_build(') <
        prog.index('static void minild_shared_output_section_links(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL phdr_policy_order')

layout_type = re.compile(r'\btypedef\s+struct\s+MiniLdSharedImage\s*\{')
owners = {name: len(layout_type.findall(source))
          for name, source in sources.items()}
if owners.get('linker_shared_pre_layout.inc') != 1 or sum(owners.values()) != 1:
    raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL shared_image_owner={owners}')

# Every resolver index has exactly one dynamic-link symbol projection.  The
# old six parallel arrays admitted independently sized/owned symbol maps.
symbol_plan_type = re.compile(r'\btypedef\s+struct\s+MiniLdSharedSymbolPlan\s*\{')
plan_owners = {name: len(symbol_plan_type.findall(source))
               for name, source in sources.items()}
if plan_owners.get('linker_shared_pre_layout.inc') != 1 or \
        sum(plan_owners.values()) != 1 or \
        len(re.findall(r'\bMiniLdSharedSymbolPlan\s*\*symbols\s*;', pre)) != 1:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_owner')
symbol_fields = ('dynsym_index', 'dynstr_name_offset', 'plt_index',
                 'got_slot_offset', 'tls_gd_offset', 'preemptible')
plan_body = re.search(r'typedef\s+struct\s+MiniLdSharedSymbolPlan\s*\{(.*?)\}',
                      pre, re.S)
if plan_body is None or any(
        len(re.findall(r'\b' + field + r'\s*;', plan_body.group(1))) != 1
        for field in symbol_fields):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_fields')
for name, code in sources.items():
    if re.search(r'\bsymbol_map_capacity\b', code) or any(
            re.search(r'\bshared->' + field + r'(?:\[|\b)', code)
            for field in symbol_fields):
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'parallel_symbol_maps={name}')
if 'shared->symbols[' not in pre or \
        'shared->symbols[' not in sources['linker_shared_reloc_scan.inc'] or \
        'shared->symbols[' not in sources['linker_shared_reloc_apply.inc'] or \
        'shared->symbols[' not in fin or \
        'shared->symbols[' not in sources['linker_version.inc']:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_consumers')

# ET_DYN only projects ordinary dynamic RELA records. The RISC-V leaf owns
# final HI/LO instruction fixups, CALL/PLT patching and checked PLT words;
# the shared mode may still reserve slots and name JUMP_SLOT relocations.
target_fixup = (src / 'linker_riscv_fixup.inc').read_text(encoding='utf-8')
shared_apply = sources['linker_shared_reloc_apply.inc']
shared_target = sources['linker_shared_riscv_target.inc']
shared_abi = sources['linker_shared_target_abi.inc']
shared_riscv_abi = sources['linker_shared_riscv_abi.inc']
final_writer = (src / 'linker_final_elf_image.inc').read_text(encoding='utf-8')
if len(re.findall(r'\bstatic\s+bool\s+minild_target_emit_plt_entry\s*\(',
                  target_fixup)) != 1 or \
        shared_target.count('minild_target_emit_plt_entry(') != 1 or \
        '!minild_target_emit_plt_entry(' not in shared_target or \
        'minild_target_emit_plt_entry(' in shared_apply or \
        re.search(r'\bR_RISCV_\w+\b|\bminild_riscv_\w+\b|'
                  r'\bminild_target_apply_(?:final_instruction|pcrel_lo12)\s*\(|'
                  r'\bshared_riscv_(?:u|i)type\b|store_u32le\(plt->data',
                  shared_apply) or \
        shared_target.count('minild_target_apply_pcrel_lo12(') != 1 or \
        shared_target.count('minild_target_apply_final_instruction(') != 2 or \
        shared_apply.count('#include "linker_shared_riscv_target.inc"') != 1 or \
        'minild_final_pack_rela(' not in shared_target or \
        re.search(r'\bElf64_Rela\s+\w+\s*;', shared_target):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL target_plt_owner')

# The currently RV64-only ET_DYN machinery has a generic layout and
# preemption pipeline; ISA classification, dynamic relocation numbering,
# instruction bytes and ELF record geometry are target-owned contracts.
target_callbacks = ('classify_relocation', 'relocation_at_final_layout',
                    'dynamic_relocation_info', 'absolute_relocation_width',
                    'store_absolute', 'patch_final_relocations',
                    'finalize_plt_and_calls')
for callback in target_callbacks:
    if len(re.findall(r'\(\*' + callback + r'\)\s*\(', shared_abi)) != 1:
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'target_abi_missing_callback={callback}')
for record in ('dynsym_record_bytes', 'rela_record_bytes',
               'dynamic_record_bytes'):
    if len(re.findall(r'\b' + record + r'\s*;', shared_abi)) != 1:
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'target_abi_missing_geometry={record}')
if 'minild_riscv64_shared_abi' not in shared_riscv_abi or \
        'minild_riscv_shared_target_abi(void)' not in shared_riscv_abi or \
        '#include "linker_shared_riscv_abi.inc"' not in shared_abi or \
        not re.search(r'state->target->shared_abi\s*\(', shared_abi):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     'target_backend_selection_boundary')

# The shared pipeline plans canonical metadata while the common ELF writer
# serializes class-specific records.  Relocation type numbering remains a
# target-specific psABI decision; there must be no host-native memcpy path.
for function in ('minild_final_pack_symbol', 'minild_final_pack_rela',
                 'minild_final_pack_dynamic'):
    if not re.search(r'\b' + function + r'\s*\(', final_writer):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'common_elf_pack_owner={function}')
for name in ('linker_shared_finalize.inc',
             'linker_shared_reloc_apply.inc'):
    source = sources[name]
    if re.search(r'\bElf64_(?:Sym|Rela|Dyn)\s+\w+\s*;', source) or \
            re.search(r'\(Elf64_(?:Sym|Rela|Dyn)\s*\*\)', source):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'host_elf64_record_in_common={name}')
if re.search(r'\(uint32_t\s*\*\)\s*\(void\s*\*\)', fin):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     'host_endian_sysv_hash_projection')

# Common must not enumerate machine-specific shared linkers.  The six
# immutable TargetProfiles select RV64 full ABI and RV32 compat-vDSO by
# callbacks; x86-64, i386, AArch64 and LoongArch deliberately lack ET_DYN
# shared-link hooks.  i386's REL implicit-addend owner is unrelated to them.
target_profile = (src / 'linker_target_elf.inc').read_text(encoding='utf-8')
linker_entry = (src / 'linker.c').read_text(encoding='utf-8')
for name, code in (('target_profile', target_profile),
                   ('shared_abi', shared_abi),
                   ('linker_entry', linker_entry)):
    if 'MiniLdTargetSharedBackend' in code or \
            re.search(r'\bMINILD_SHARED_(?:RISCV|UNSUPPORTED)', code) or \
            re.search(r'\bshared_backend\b', code):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'architecture_enum_in_common={name}')
if not re.search(r'\(\*shared_abi\)\s*\(void\)', target_profile) or \
        not re.search(r'\bMiniLdTargetSharedLink\s+link_shared\s*;', target_profile) or \
        not re.search(r'\btarget->link_shared\s*\(', linker_entry):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     'target_shared_callbacks_missing')
profile_rows = {
    row_name: row_body
    for row_name, row_body in re.findall(
        r'\{"([^"]+)"(.*?)\}(?:,|\s*$)',
        target_profile.split('static const MiniLdTargetProfile minild_target_profiles[] = {', 1)[1]
                      .split('\n};', 1)[0], re.S)
}
expected_profiles = {
    'elf64lriscv': ('ELFCLASS64', 'EM_RISCV'),
    'elf32lriscv': ('ELFCLASS32', 'EM_RISCV'),
    'elf_x86_64': ('ELFCLASS64', 'EM_X86_64'),
    'elf_i386': ('ELFCLASS32', 'EM_386'),
    'aarch64linux': ('ELFCLASS64', 'EM_AARCH64'),
    'elf64loongarch': ('ELFCLASS64', 'EM_LOONGARCH'),
}
if set(profile_rows) != set(expected_profiles):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     f'target_profile_count={sorted(profile_rows)}')
for emulation, (elf_class, machine) in expected_profiles.items():
    if not re.search(r'^\s*,\s*' + elf_class + r'\s*,\s*ELFDATA2LSB\s*,\s*' +
                     machine + r'\s*,', profile_rows[emulation]):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'target_profile_abi={emulation}')
if not re.search(r'minild_riscv_shared_target_abi\s*,\s*'
                 r'minild_link_shared_elf64_riscv_inputs_options',
                 profile_rows['elf64lriscv']) or \
        not re.search(r'NULL\s*,\s*minild_link_elf32_compat_vdso',
                      profile_rows['elf32lriscv']):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     'riscv_shared_backend_bindings')
for emulation in ('elf_x86_64', 'aarch64linux', 'elf64loongarch'):
    if not re.search(r'NULL\s*,\s*NULL\s*,\s*'
                     r'minild_\w+_elf_flags\s*,\s*(?:true|false)\s*,\s*'
                     r'NULL\s*$',
                     profile_rows[emulation].strip()):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'unsupported_shared_hooks={emulation}')
if not re.search(r'NULL\s*,\s*NULL\s*,\s*'
                 r'minild_zero_elf_flags\s*,\s*true\s*,\s*'
                 r'minild_i386_implicit_addend\s*$',
                 profile_rows['elf_i386'].strip()):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     'unsupported_shared_hooks=elf_i386')
for name in ('linker_shared_reloc_scan.inc',
             'linker_shared_reloc_apply.inc'):
    code = sources[name]
    if '->classify_relocation(' not in code or re.search(
            r'\bminild_target_reloc_class\s*\(', code):
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'common_machine_classifier={name}')
for name, callback in (('linker_shared_output.inc', 'patch_final_relocations'),
                       ('linker_shared_output.inc', 'finalize_plt_and_calls'),
                       ('linker_shared_reloc_apply.inc', 'dynamic_relocation_info')):
    if '->' + callback + '(' not in sources[name]:
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'missing_target_dispatch={name}:{callback}')
for kind, typename, expected_owner in (
        ('struct', 'MiniLdSharedTlsSegment', 'linker_shared_program_plan.inc'),
        ('enum', 'MiniLdSharedRelocScanResult',
         'linker_shared_reloc_scan.inc')):
    declaration = re.compile(r'\btypedef\s+' + kind + r'\s+' +
                             typename + r'\s*\{')
    actual = {name: len(declaration.findall(code))
              for name, code in sources.items()}
    if actual.get(expected_owner) != 1 or sum(actual.values()) != 1:
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'type={typename} owners={actual}')

functions = {
    'linker_shared_pre_layout.inc': (
        'shared_image_destroy', 'shared_elf_hash',
        'shared_gnu_symbol_hash', 'shared_symbol_eligible',
        'shared_symbol_preemptible', 'shared_symbol_needs_plt',
        'shared_define_linker_symbols_pre_layout',
        'shared_prepare_metadata',
    ),
    'linker_shared_finalize.inc': (
        'shared_finalize_ehdr_symbol', 'shared_fill_dynsym',
        'shared_fill_hash', 'shared_fill_gnu_hash', 'shared_fill_dynamic',
    ),
    'linker_shared_program_plan.inc': (
        'shared_tls_segment', 'minild_shared_program_plan_build',
        'minild_shared_output_section_links',
    ),
    'linker_shared_output.inc': (
        'shared_write_object', 'shared_validate_pie_dso_symbols',
        'shared_strip_debug_preflight',
        'shared_script_validate_discarded_alloc',
        'minild_link_shared_elf64_riscv_inputs_options',
        'minild_link_shared_elf64_riscv_inputs',
    ),
    'linker_shared_reloc_apply.inc': (
        'shared_fill_relocations',
    ),
    'linker_shared_riscv_target.inc': (
        'riscv_shared_patch_final_layout_relocations',
        'riscv_shared_finalize_plt_and_calls',
    ),
}
definitions = 0
for owner, names in functions.items():
    for func in names:
        pattern = re.compile(
            r'\b(?:static\s+)?(?:bool|void|int|size_t|uint32_t|uint64_t)'
            r'\s+' + re.escape(func) + r'\s*\([^;{}]*\)\s*\{', re.S,
        )
        locations = {module: len(pattern.findall(text))
                     for module, text in sources.items()}
        if locations.get(owner) != 1 or sum(locations.values()) != 1:
            raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                             f'function={func} definitions={locations}')
        definitions += 1

scan_function = re.compile(
    r'\bstatic\s+MiniLdSharedRelocScanResult\s+'
    r'shared_scan_relocation_requirements\s*\([^;{}]*\)\s*\{',
    re.S,
)
scan_owners = {name: len(scan_function.findall(code))
               for name, code in sources.items()}
if scan_owners.get('linker_shared_reloc_scan.inc') != 1 or \
        sum(scan_owners.values()) != 1:
    raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     f'function=shared_scan_relocation_requirements '
                     f'definitions={scan_owners}')
definitions += 1

for func in ('shared_prepare_metadata', 'shared_build_vdso_script_layout',
             'shared_fill_dynsym', 'shared_fill_relocations',
             'shared_fill_dynamic', 'shared_write_object'):
    if func + '(' not in driver:
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'driver_missing_call={func}')

print('MINILD_SHARED_PHASE_STRUCTURE=PASS modules=5 '
      f'owner_functions={definitions} shared_image=single '
      'symbol_plan=single target_plt=1 target_instruction_fixups=1 '
      'shared_target_hooks=7 record_sizes=3 common_elf_packers=3 '
      'backend_callbacks=5 build=single_translation_unit '
      'order=reserve,fill,program,write')
PY

# Each of these existing integration fixtures exercises actual ELF content,
# PHDRs, dynamic tables, symbols, relocations or failure contracts.  The
# architecture move neither extends ISA coverage nor adds ET_DYN features.
fixtures=(
    'probe-shared-output-section.sh:MINILD_SHARED_OUTPUT'
    'probe-shared-script-dynsym.sh:MINILD_SCRIPT_DYNSYM'
    'probe-shared-input-identity.sh:MINILD_SHARED_INPUT'
    'probe-shared-discard-inputs.sh:MINILD_SHARED_DISCARD'
    'probe-shared-synthetic-discard.sh:MINILD_SHARED_SYNTHETIC_DISCARD'
    'probe-shared-textrel-pie.sh:MINILD_SHARED_TEXTREL_PIE'
    'probe-shared-nonalloc-relocations.sh:MINILD_SHARED_NONALLOC_RELOC'
    'probe-script-nonalloc-offset.sh:MINILD_SHARED_NONALLOC_STRIP'
    'probe-segment-plan.sh:MINILD_SEGMENTS'
    'probe-final-symbol-view.sh:MINILD_FINAL_SYMBOL'
    'probe-output-plan-owner-lifecycle.sh:MINILD_OUTPUT_PLAN_OWNER'
    'probe-final-elf64-owner-lifecycle.sh:MINILD_FINAL_ELF64_OWNER'
    'probe-riscv-final-fixup-parity.sh:MINILD_RISCV_FINAL_FIXUP'
    'probe-reloc-owner-lifecycle.sh:MINILD_RELOC_OWNER'
)

passed=0
for fixture in "${fixtures[@]}"; do
    script=${fixture%%:*}
    marker=${fixture#*:}
    logfile="$work/${script%.sh}.log"
    if ! bash "$repo/tests/linker/$script" >"$logfile" 2>&1 || \
       ! grep -Eq "^${marker}=PASS([[:space:]]|$)" "$logfile"; then
        echo "MINILD_SHARED_PHASE=FAIL fixture=$script expected=$marker" >&2
        tail -n 60 "$logfile" >&2
        exit 1
    fi
    printf 'MINILD_SHARED_PHASE_CHECK=PASS fixture=%s\n' "$script"
    ((passed += 1))
done
echo "MINILD_SHARED_PHASE=PASS fixtures=$passed modules=5 compiler=Werror"

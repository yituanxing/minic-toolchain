#!/usr/bin/env python3
"""Strict complete merge of seven real MiniPP->MiniC->MiniAS shard products."""
import argparse,hashlib,json,re,shutil,struct
from pathlib import Path
from linux_minipp_seven_balanced_plan_v1 import balanced_members
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''):h.update(chunk)
    return h.hexdigest()
p=argparse.ArgumentParser()
for name in ['artifacts','owners','golden-out','dest','commit']:p.add_argument('--'+name,required=True)
p.add_argument('--pinned-profile',type=Path)
p.add_argument('--native-macro-rules',action='store_true')
p.add_argument('--balanced-plan',type=Path)
a=p.parse_args()
pinned_sha=sha(a.pinned_profile) if a.pinned_profile else None
if a.native_macro_rules and not pinned_sha:
    raise SystemExit('MINIPP_MERGE=FAIL native-macro-rules-missing-pinned-base-sha')

root,ownerfile,gold,dest=map(Path,[a.artifacts,a.owners,a.golden_out,a.dest])
owners=[x.strip() for x in ownerfile.read_text().splitlines() if x.strip() and not x.startswith('#')]
if len(owners)!=2064 or len(set(owners))!=2064:raise SystemExit('MINIPP_MERGE=FAIL owners')
balanced=(balanced_members(owners,a.balanced_plan) if a.balanced_plan else None)
balanced_sha=(sha(a.balanced_plan) if a.balanced_plan else None)
if not re.fullmatch('[0-9a-f]{40}',a.commit):raise SystemExit('MINIPP_MERGE=FAIL commit')
dest.mkdir(parents=True,exist_ok=True)
records={}; failures=0
# Performance is diagnostic only; never weaken SHA/ABI/ownership acceptance.
# No wall time can be inferred by summing seven concurrently running jobs.
performance_rows=[]
for n in range(7):
    directory=root/f'minipp-native-shard-{n}-{a.commit}'
    if not directory.is_dir():raise SystemExit(f'MINIPP_MERGE=FAIL missing_shard={n}')
    summary=json.loads((directory/'summary.json').read_text())
    rows=json.loads((directory/'results.json').read_text())
    expected_profile=('native_rules' if a.native_macro_rules else 'pinned')
    if pinned_sha and (summary.get('builtin_profile') != expected_profile or
                       summary.get('builtin_profile_sha') != pinned_sha):
        raise SystemExit(f'MINIPP_MERGE=FAIL wrong_macro_producer_worker={n}')
    selected=(balanced[n] if balanced is not None else owners[n::7])
    if summary.get('balanced_plan_sha256') != balanced_sha:
        raise SystemExit(f'MINIPP_MERGE=FAIL balanced_plan_identity_shard={n}')
    if (summary['shard']!=n or summary['shards']!=7 or not summary['full_shard'] or
        summary['status']!='PASS' or summary['full_universe']!=2064 or
        summary['selected']!=len(selected) or summary['passed']!=len(selected) or
        summary['failed'] or [x['object'] for x in rows]!=selected):
        raise SystemExit(f'MINIPP_MERGE=FAIL incomplete_worker={n}')
    manifest=directory/'native-manifest.sha256'
    if sha(manifest)!=summary['manifest_sha']:
        raise SystemExit(f'MINIPP_MERGE=FAIL artifact_identity={n}')
    pairs=[x.split('  ',1) for x in manifest.read_text().splitlines()]
    if len(pairs)!=len(rows):raise SystemExit(f'MINIPP_MERGE=FAIL manifest_count={n}')
    for row,(hashval,relative) in zip(rows,pairs):
        owner=row['object']
        if (owner in records or row['status']!='PASS' or
            relative!=f'native-objects/{owner}' or hashval!=row['native_object_sha'] or
            row['mini_pp_sha']!=row['gnu_pp_sha']):
            raise SystemExit(f'MINIPP_MERGE=FAIL PP_provenance={owner}')
        obj=directory/'native-objects'/owner
        frozen=gold/owner
        if not obj.is_file() or not frozen.is_file() or sha(obj)!=hashval or sha(frozen)!=row['frozen_original_sha']:
            raise SystemExit(f'MINIPP_MERGE=FAIL ELF_hash={owner}')
        raw,ref=obj.read_bytes()[:52],frozen.read_bytes()[:52]
        if len(raw)!=52 or raw[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<HH',raw,16)!=(1,243) or raw[48:52]!=ref[48:52]:
            raise SystemExit(f'MINIPP_MERGE=FAIL ELF_ABI={owner}')
        target=dest/owner
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(obj,target)
        records[owner]=hashval
        performance_rows.append(row)
    print(f'MINIPP_SEVEN_MERGE_SHARD=PASS shard={n}/7 objects={len(selected)}',flush=True)
if set(records)!=set(owners):raise SystemExit('MINIPP_MERGE=FAIL incomplete_2064')
(dest.parent/'native-full-manifest.sha256').write_text(''.join(f'{records[o]}  {o}\n' for o in owners))
(dest.parent/'native-summary.json').write_text(json.dumps(dict(owners=2064,shards=7,failed=0,pp='MiniPP',
                    cc='MiniC',assembler='MiniAS',qemu_certified=False,
                    builtin_profile='native_rules' if a.native_macro_rules else 'pinned' if pinned_sha else 'reference',
                    builtin_profile_sha=pinned_sha,
                    generated_header_replay_requires_MiniPP=True),indent=2)+'\n')
# Build one global performance report from the exact 2064 already-authenticated
# owner rows. This is an optional telemetry layer, NEVER a pass bypass.
stages=('macro_predefines','gnu_pp_oracle','minipp','minic','minias')
complete=[row for row in performance_rows
          if all(isinstance(row.get('timings_ms',{}).get(stage),int)
                 for stage in ('minipp','minic','minias'))
          and all(isinstance(row.get('peak_rss_kb',{}).get(stage),int)
                  for stage in ('minipp','minic','minias'))]
performance={'schema':'minipp-linux-performance-v1','owners':len(records),
             'balanced_plan_sha256':balanced_sha,
             'assignment_mode':'balanced_sha_bound' if balanced is not None else 'legacy_stride',
             'measured_owners':len(complete),'source_commit':a.commit,
             'measurement_scope':'sum of individual subprocess times; NOT concurrent wall-clock',
             'peak_rss_scope':'maximum individual Linux wait4 ru_maxrss; NOT job/cgroup RAM'}
if complete:
    def pct(sorted_values,p):
        # Nearest-rank percentile, stable with any cohort size.
        import math
        return sorted_values[max(0,math.ceil(len(sorted_values)*p/100)-1)]
    performance['stages']={}
    for stage in stages:
        times=sorted(row.get('timings_ms',{}).get(stage)
                     for row in complete if stage in row.get('timings_ms',{}))
        if not times:
            continue
        rss=[row['peak_rss_kb'][stage] for row in complete
             if stage in row.get('peak_rss_kb',{})]
        performance['stages'][stage]={
            'measured_count':len(times),'total_process_ms':sum(times),
            'p50_process_ms':pct(times,50),'p95_process_ms':pct(times,95),
            'max_process_ms':max(times),
            'max_individual_process_rss_kb':max(rss) if rss else None}
    performance['slowest_minic_objects']=[
       {'object':row['object'],
        'minic_ms':row['timings_ms']['minic'],
        'minic_peak_rss_kb':row['peak_rss_kb']['minic'],
        'preprocessed_bytes':row.get('minipp_input_bytes'),
        'assembly_bytes':row.get('minic_assembly_bytes')}
       for row in sorted(complete,key=lambda r:r['timings_ms']['minic'],
                         reverse=True)[:20]]
    performance['largest_minic_memory_objects']=[
       {'object':row['object'],
        'minic_peak_rss_kb':row['peak_rss_kb']['minic'],
        'minic_ms':row['timings_ms']['minic']}
       for row in sorted(complete,key=lambda r:r['peak_rss_kb']['minic'],
                         reverse=True)[:20]]
    performance['native_output_object_bytes']=sum(
        row.get('native_object_bytes',0) for row in complete)
(dest.parent/'native-performance.json').write_text(
    json.dumps(performance,indent=2,sort_keys=True)+'\n')
print(f"MINIPP_SEVEN_PERFORMANCE_MERGE={('PASS' if len(complete)==2064 else 'PARTIAL')} "
      f"measured={len(complete)}/2064 owner_scoped=true "
      "rss=max_individual_process no_concurrent_job_wall_claim=true",flush=True)
print('MINIPP_SEVEN_MERGE=PASS owners=2064 shards=7 GNU_PP_consumed_by_MiniC=false QEMU_pending=true '
      + ('assignment=balanced_sha_bound' if balanced is not None else 'assignment=legacy_stride'),flush=True)
if a.native_macro_rules:
    assert pinned_sha and len(records) == 2064
    print('MINIPP_NATIVE_MACRO_MERGE=PASS owners=2064 shards=7 '
          'macro_producer=native_abi_rules '
          'snapshot_SHA_attested=true GCC_macro_generation=none '
          'QEMU_pending=true',flush=True)

#!/usr/bin/env python3
"""Strict complete merge of seven real MiniPP->MiniC->MiniAS shard products."""
import argparse,hashlib,json,re,shutil,struct
from pathlib import Path
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''):h.update(chunk)
    return h.hexdigest()
p=argparse.ArgumentParser()
for name in ['artifacts','owners','golden-out','dest','commit']:p.add_argument('--'+name,required=True)
a=p.parse_args()
root,ownerfile,gold,dest=map(Path,[a.artifacts,a.owners,a.golden_out,a.dest])
owners=[x.strip() for x in ownerfile.read_text().splitlines() if x.strip() and not x.startswith('#')]
if len(owners)!=2057 or len(set(owners))!=2064:raise SystemExit('MINIPP_MERGE=FAIL owners')
if not re.fullmatch('[0-9a-f]{40}',a.commit):raise SystemExit('MINIPP_MERGE=FAIL commit')
dest.mkdir(parents=True,exist_ok=True)
records={}; failures=0
for n in range(7):
    directory=root/f'minipp-native-shard-{n}-{a.commit}'
    if not directory.is_dir():raise SystemExit(f'MINIPP_MERGE=FAIL missing_shard={n}')
    summary=json.loads((directory/'summary.json').read_text())
    rows=json.loads((directory/'results.json').read_text())
    selected=owners[n::7]
    if (summary['shard']!=n or summary['shards']!=7 or not summary['full_shard'] or
        summary['status']!='PASS' or summary['full_universe']!=2057 or
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
    print(f'MINIPP_SEVEN_MERGE_SHARD=PASS shard={n}/7 objects={len(selected)}',flush=True)
if set(records)!=set(owners):raise SystemExit('MINIPP_MERGE=FAIL incomplete_2057')
(dest.parent/'native-full-manifest.sha256').write_text(''.join(f'{records[o]}  {o}\n' for o in owners))
(dest.parent/'native-summary.json').write_text(json.dumps(dict(owners=2057,shards=7,failed=0,pp='MiniPP',
                    cc='MiniC',assembler='MiniAS',qemu_certified=False,
                    generated_header_replay_requires_MiniPP=True),indent=2)+'\n')
print('MINIPP_SEVEN_MERGE=PASS owners=2064 shards=7 GNU_PP_consumed_by_MiniC=false QEMU_pending=true',flush=True)

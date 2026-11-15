"""Validate final warm/cold A/B conditions and first useful input, using existing logs."""
import csv,hashlib,json,pathlib,re
V=pathlib.Path(__file__).resolve().parent
hashes={'reference':'1c29852e682db65a01590674986e00c937ee14a63dc44d053700ff4a325393a6','candidate':'872dd340826f39da1bb14371681bb0decc4f4d3c640ae93e646ef46337465b55'}
changes=('ES_SOFT_OPAQUE_FADE','ES_SOFT_UPLOAD_NEON','ES_SOFT_COPY_PLD_PAIR','ES_SOFT_ALPHA_SCAN','ES_SYSTEM_THREADS','ES_TEXTURE_THREADS')
reports=[];catalogs=set()
for mode in ('warm','cold'):
    paths=sorted(p for p in (V/'target-results').glob(f'startup-full-{mode}-v1-*') if p.is_dir())
    assert len(paths)==4,(mode,len(paths));common=None;boots=[]
    for d in paths:
        c=json.loads((d/'configuration.json').read_text());e=c['renderer_environment'];candidate='-candidate-' in d.name
        kind='candidate' if candidate else 'reference'
        assert c['binary_sha256']==hashes[kind] and c['binary_sha_verified_after_run']
        assert c['wrapper'] and c['core']=='MENU' and c['framebuffer_size']=='1920,1080' and c['requested_size']=='1920x1080'
        for k,v in zip(changes,('1','1','1','0','2','1') if candidate else ('0','0','0','0','4','2')):assert e[k]==v,(d.name,k)
        for k,v in {'ES_SOFT_FPS':'60','ES_SOFT_STRIP':'8','ES_SOFT_PLD':'256','ES_SOFT_THREADS':'2','ES_SOFT_OPTIMIZATIONS':'1'}.items():assert e[k]==v
        for k in ('ES_SOFT_DIRTY_CHECK','ES_SOFT_DUMP','ES_SOFT_SLOW_DUMP','ES_PROFILE','ES_SOFT_BENCH','ES_AUDIT_FIXED_DT','ES_TEXTURE_TRACE'):assert k not in e
        assert not list(d.glob('*.png'))
        text=(d/'es-startup.log').read_text();shown=next(line.split('systems shown: ',1)[1] for line in text.splitlines() if 'systems shown: ' in line);catalogs.add(shown)
        signature=dict(config=c['config_sha256'],catalog=shown,script=hashlib.sha256((d/'navigation.txt').read_bytes()).hexdigest(),environment={k:v for k,v in e.items() if k not in changes and k not in ('ES_SOFT_SCRIPT','ES_SOFT_TRACE','ES_SOFT_SHOTS','ES_SOFT_STATS_FILE')})
        if common is None:common=signature
        else:assert signature==common,'Uncontrolled startup condition: '+d.name
        inputs=list(csv.DictReader((d/'trace.csv.input.csv').open()));moves=re.findall(r'input selection result: (\d+) -> (\d+)',text)
        assert len(inputs)==len(moves)==1 and moves[0][0]!=moves[0][1]
        memory=json.loads((d/'memory.json').read_text());assert max(r['framebuffer_owners'] for r in memory)==1 and not (d/'framebuffer-collision.json').exists()
        if mode=='cold':
            assert not c['elf_read_before_spawn'] and not c['frontend_owners_before_setup'] and not c['frontend_launchers_before_setup']
            event=json.loads((V/'boot-events'/(d.name.rsplit('-1-t',1)[0]+'.json')).read_text())
            assert event['before']['boot_id']!=event['after']['boot_id']==c['boot_id']
            assert not event.get('frontend_before_first_opening',{}).get('owners') and not event.get('frontend_before_first_opening',{}).get('launchers')
            boots.append(c['boot_id'])
        result=json.loads((d/'startup-summary.json').read_text())
        reports.append(dict(run=d.name,implementation=kind,mode=mode,conditions_verified=True,selection= moves[0],wrapper_to_first_input_ms=result['wrapper_to_first_input_ms']))
    if mode=='cold':assert len(set(boots))==4,'Cold runs must use four distinct Linux boots'
assert len(catalogs)==1,'Warm and cold catalog/order changed'
(V/'startup-final-conditions.json').write_text(json.dumps(dict(runs=reports,ordered_catalog=next(iter(catalogs))),indent=2))
print('Eight full-wrapper openings verified: same catalog/config/1080p/renderer, ABBA; four distinct cold boots, no prior frontend or ELF preload; useful input in each.')

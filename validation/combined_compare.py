"""Final combined acceptance: explicit binaries, validated configuration, raw inputs."""
import argparse,bisect,collections,csv,hashlib,json,math,pathlib,statistics
p=argparse.ArgumentParser()
p.add_argument('runs',nargs='+');p.add_argument('--reference-sha',required=True)
p.add_argument('--candidate-sha',required=True);p.add_argument('--expected',type=int,default=1000)
p.add_argument('--alpha',choices=('0','1'),required=True)
p.add_argument('--copy',choices=('0','1'),required=True);p.add_argument('--output',required=True)
p.add_argument('--texture',choices=('1','2'),default='2',help='Candidate decoder policy; reference retains two')
a=p.parse_args();assert a.reference_sha!=a.candidate_sha
def stats(values):
    s=sorted(values)
    return dict(n=len(s),median=round(statistics.median(s),3),p95=round(s[math.ceil(.95*len(s))-1],3),
                p99=round(s[math.ceil(.99*len(s))-1],3),max=round(s[-1],3)) if s else dict(n=0)
def rows(path):return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
common=None;common_cursor=None;results=[]
changes=('ES_SOFT_OPAQUE_FADE','ES_SOFT_UPLOAD_NEON','ES_SOFT_COPY_PLD_PAIR','ES_SOFT_ALPHA_SCAN','ES_SYSTEM_THREADS','ES_TEXTURE_THREADS')
for name in a.runs:
    d=pathlib.Path(name);c=json.loads((d/'configuration.json').read_text());e=c['renderer_environment']
    assert c['binary_sha256'] in (a.reference_sha,a.candidate_sha)
    candidate=c['binary_sha256']==a.candidate_sha
    expected=dict(zip(changes,('1','1',a.copy,a.alpha,'2',a.texture) if candidate else ('0','0','0','0','4','2')))
    for k,v in expected.items():assert e.get(k)==v,(d.name,k,e.get(k),v)
    assert c['framebuffer_size']=='1920,1080' and c['requested_size']=='1920x1080'
    assert e['ES_SOFT_STRIP']=='8' and e['ES_SOFT_PLD']=='256' and e['ES_SOFT_FPS']=='60'
    assert e['ES_SOFT_THREADS']=='2' and e['ES_SOFT_OPTIMIZATIONS']=='1'
    for k in ('ES_SOFT_DIRTY_CHECK','ES_SOFT_DUMP','ES_SOFT_SLOW_DUMP','ES_PROFILE','ES_SOFT_BENCH','ES_TEXTURE_TRACE','ES_AUDIT_FIXED_DT'):
        assert k not in e,(d.name,k)
    assert not list(d.glob('*.png'))
    startup=(d/'startup.log').read_text()
    shown=next(line.split('systems shown: ',1)[1] for line in startup.splitlines() if 'systems shown: ' in line)
    signature=dict(config=c['config_sha256'],ordered_systems_and_game_counts=shown,navigation_sha256=hashlib.sha256((d/'navigation.txt').read_bytes()).hexdigest(),environment={k:v for k,v in e.items() if k not in changes and k not in ('ES_SOFT_TRACE','ES_SOFT_SHOTS','ES_SOFT_SCRIPT','ES_SOFT_STATS_FILE')})
    if common is None:common=signature
    else:assert signature==common,'Uncontrolled condition difference: '+d.name
    inputs=rows(d/'trace.csv.input.csv');assert len(inputs)==a.expected,(d.name,len(inputs))
    begin=min(r['at_ns']-r['event_ns'] for r in inputs);end=max(r['at_ns'] for r in inputs)+800_000_000
    by_frame=collections.defaultdict(list)
    for r in rows(d/'trace.csv'):by_frame[r['frame']].append(r)
    assert len(by_frame)<8192,'Trace saturated'
    timeline=sorted((ws[0] for ws in by_frame.values()),key=lambda r:r['at_ns']);times=[r['at_ns'] for r in timeline]
    initial=max((r for r in timeline if r['at_ns']<begin and r['system_cursor']>=0),key=lambda r:r['at_ns'])['system_cursor']
    if common_cursor is None:common_cursor=initial
    else:assert initial==common_cursor,'Initial system differs between runs'
    keys=[line.split()[1] for line in (d/'navigation.txt').read_text().splitlines() if line.startswith('key ')]
    assert len(keys)==len(inputs) and all(key in ('Right','Left') for key in keys)
    expected_cursor=initial;observed_cursors=[]
    for key,event in zip(keys,inputs):
        expected_cursor+=1 if key=='Right' else -1
        response=timeline[bisect.bisect_right(times,event['at_ns'])-1]
        assert response['system_cursor']==expected_cursor,(d.name,len(observed_cursors)+1,key,expected_cursor,response['system_cursor'])
        observed_cursors.append(response['system_cursor'])
    selected=[ws for ws in by_frame.values() if begin<=ws[0]['at_ns']<=end and ws[0]['redraw']]
    full=[ws for ws in selected if not ws[0]['partial']]
    work=[sum(ws[0][k] for k in ('input_ns','update_ns','record_ns','dirty_ns','execute_ns','post_ns'))/1e6 for ws in selected]
    report=dict(run=d.name,implementation='candidate' if candidate else 'reference',responded=len(inputs),
        boot_id=c.get('boot_id'),uptime_before_spawn_s=c.get('uptime_before_spawn_s'),
        initial_system_cursor=initial,verified_selection_changes=len(observed_cursors),visited_cursors=sorted(set(observed_cursors)),
        first_injection_ns=begin,last_response_ns=max(r['at_ns'] for r in inputs),
        sdl_first_redraw_ms=stats([r['event_ns']/1e6 for r in inputs]),
        dispatch_first_redraw_ms=stats([r['dispatch_ns']/1e6 for r in inputs]),
        full_renderer_ms=stats([ws[0]['execute_ns']/1e6 for ws in full]),
        drawn_frame_work_ms=stats(work),drawn_frames=len(selected),full_frames=len(full),
        drawn_above_16_7_ms=sum(v>1000/60 for v in work))
    stages={}
    for k in ('input_ns','update_ns','record_ns','dirty_ns','post_ns','fps_limit_ns','pacing_ns'):
        stages[k]=stats([ws[0][k]/1e6 for ws in full])
    stages['draw_max_ms']=stats([max(w['drawn_ns']-w['start_ns'] for w in ws)/1e6 for ws in full])
    stages['copy_max_ms']=stats([max(w['copied_ns']-w['drawn_ns'] for w in ws)/1e6 for ws in full])
    stages['off_cpu_draw_max_ms']=stats([max(w['drawn_ns']-w['start_ns']-w['cpu_draw_ns'] for w in ws)/1e6 for ws in full])
    report['full_stages']=stages
    memory=json.loads((d/'memory.json').read_text())
    assert not (d/'framebuffer-collision.json').exists(),'Overlapping frontend owners'
    assert memory and max(r.get('framebuffer_owners',0) for r in memory)==1,'Missing or invalid owner guard'
    report['rss_peak_mib']=round(max(r.get('VmRSS',0) for r in memory)/1024,3)
    results.append(report);print(json.dumps(report),flush=True)
groups={}
for kind in ('reference','candidate'):
    group=[r for r in results if r['implementation']==kind];assert len(group)>=2
    groups[kind]={k:stats([r[k]['median'] for r in group]) for k in ('sdl_first_redraw_ms','full_renderer_ms')}
gaps=[round((right['first_injection_ns']-left['last_response_ns'])/1e9,3) for left,right in zip(results,results[1:])]
pathlib.Path(a.output).write_text(json.dumps(dict(conditions=common,reference_sha=a.reference_sha,candidate_sha=a.candidate_sha,runs=results,run_medians=groups,inter_run_gaps_s=gaps),indent=2))

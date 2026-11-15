"""Analyze one-variable pairs, using SDL input timestamps to bound navigation."""
import argparse,collections,csv,json,math,pathlib,re,statistics
p=argparse.ArgumentParser()
p.add_argument('runs',nargs='+');p.add_argument('--expected',type=int,required=True)
p.add_argument('--output',required=True)
p.add_argument('--parameter',choices=('ES_SOFT_OPAQUE_FADE','ES_SOFT_SCALED_MB','ES_SOFT_COPY_PLD_PAIR','ES_SOFT_ALPHA_SCAN'),default='ES_SOFT_OPAQUE_FADE')
p.add_argument('--allow-binary-change',action='store_true',help='Only for explicitly identified independent source reference comparisons')
args=p.parse_args()
def stats(values):
    a=sorted(values)
    if not a:return {'n':0}
    return dict(n=len(a),median=round(statistics.median(a),3),
        p95=round(a[math.ceil(len(a)*.95)-1],3),p99=round(a[math.ceil(len(a)*.99)-1],3),max=round(a[-1],3))
records=[];same={}
for name in args.runs:
    d=pathlib.Path(name);c=json.loads((d/'configuration.json').read_text())
    env=c['renderer_environment'];choice=env[args.parameter]
    signature=dict(binary=c['binary_sha256'],config=c['config_sha256'],size=c['framebuffer_size'],
        environment={k:v for k,v in env.items() if k not in (args.parameter,'ES_SOFT_TRACE','ES_SOFT_SCRIPT','ES_SOFT_SHOTS','ES_SOFT_STATS_FILE')})
    if args.allow_binary_change:signature.pop('binary')
    if same:assert same==signature,'Conditions differ: '+name
    else:same=signature
    for forbidden in ('ES_SOFT_DIRTY_CHECK','ES_SOFT_DUMP','ES_SOFT_SLOW_DUMP','ES_PROFILE','ES_SOFT_BENCH','ES_AUDIT_FIXED_DT'):
        assert forbidden not in env,'Timing run has diagnostics enabled: '+forbidden
    assert not list(d.glob('*.png')),'Timing run contains PNG captures'
    inputs=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((d/'trace.csv.input.csv').open())]
    assert len(inputs)==args.expected,(d.name,len(inputs),args.expected)
    begin=min(r['at_ns']-r['event_ns'] for r in inputs)
    end=max(r['at_ns'] for r in inputs)+800_000_000
    frames=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((d/'trace.csv').open()) if r['worker']=='0']
    assert len(frames)<8192,'Trace frame buffer saturated'
    nav=[r for r in frames if begin<=r['at_ns']<=end and r['redraw']]
    full=[r for r in nav if not r['partial']]
    work=[sum(r[k] for k in ('input_ns','update_ns','record_ns','dirty_ns','execute_ns','post_ns'))/1e6 for r in nav]
    result=dict(run=d.name,enabled=choice,responded=len(inputs),
        sdl_first_redraw_ms=stats([r['event_ns']/1e6 for r in inputs]),
        dispatch_first_redraw_ms=stats([r['dispatch_ns']/1e6 for r in inputs]),
        full_renderer_ms=stats([r['execute_ns']/1e6 for r in full]),
        full_ui_active_ms=stats([sum(r[k] for k in ('input_ns','update_ns','record_ns'))/1e6 for r in full]),
        drawn_frame_work_ms=stats(work),drawn_above_16_7_ms=sum(v>1000/60 for v in work),
        drawn_frames=len(nav),full_frames=len(full))
    memory=json.loads((d/'memory.json').read_text())
    result['rss_peak_mib']=round(max(r.get('VmRSS',0) for r in memory)/1024,3)
    records.append(result);print(json.dumps(result))
groups=collections.defaultdict(list)
for r in records:groups[r['enabled']].append(r)
summary={k:{field:stats([r[field]['median'] for r in rows]) for field in ('sdl_first_redraw_ms','full_renderer_ms','drawn_frame_work_ms')} for k,rows in groups.items()}
pathlib.Path(args.output).write_text(json.dumps(dict(conditions=same,runs=records,run_medians=summary),indent=2))
print(json.dumps(summary,indent=2))

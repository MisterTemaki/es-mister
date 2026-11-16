import argparse,collections,csv,json,pathlib,statistics,math
p=argparse.ArgumentParser();p.add_argument('run');args=p.parse_args()
run=pathlib.Path(args.run)
rows=list(csv.DictReader((run/'trace.csv').open()))
frames=collections.defaultdict(list)
for row in rows:
    for key in row:row[key]=int(row[key])
    frames[row['frame']].append(row)
origin=min(r['at_ns'] for r in rows)
def stats(values):
    a=sorted(values)
    if not a:return dict(n=0)
    result=dict(n=len(a),min=round(a[0],3),median=round(statistics.median(a),3),mean=round(statistics.mean(a),3),
                p95_sample=round(a[math.ceil(len(a)*.95)-1],3),max=round(a[-1],3))
    if len(a)>=1000:result['p99']=round(a[math.ceil(len(a)*.99)-1],3)
    return result
def summarize(selected):
    values=collections.defaultdict(list)
    for workers in selected:
        f=workers[0]
        for k in ('ui_ns','dirty_ns','execute_ns','post_ns','diagnostics_ns','pacing_ns'):
            values[k.removesuffix('_ns')+'_ms'].append(f[k]/1e6)
        values['draw_max_ms'].append(max(w['drawn_ns']-w['start_ns'] for w in workers)/1e6)
        values['copy_max_ms'].append(max(w['copied_ns']-w['drawn_ns'] for w in workers)/1e6)
        values['wake_max_ms'].append(max(w['start_ns']-f['published_ns'] for w in workers)/1e6)
        values['off_cpu_draw_max_ms'].append(max((w['drawn_ns']-w['start_ns'])-w['cpu_draw_ns'] for w in workers)/1e6)
    return {k:stats(v) for k,v in values.items()}
active=[w for w in frames.values() if w[0]['redraw'] and w[0]['start_ns']]
navigation=[w for w in active if (w[0]['at_ns']-origin)/1e9>=14]
full=[w for w in navigation if not w[0]['partial']]
partial=[w for w in navigation if w[0]['partial']]
result=dict(all_active=summarize(active),navigation=summarize(navigation),full_navigation=summarize(full),partial_navigation=summarize(partial))
commands=list(csv.DictReader((run/'trace.csv.commands.csv').open()))
nav_ids={w[0]['frame'] for w in navigation}
grouped=collections.defaultdict(lambda:dict(ns=0,calls=0,commands=0))
for row in commands:
    r={k:int(v) for k,v in row.items()}
    if r['frame'] not in nav_ids:continue
    key=f"texture {r['tex_w']}x{r['tex_h']}, vertices {r['vertices']}, first alpha {r['first_alpha']}"
    g=grouped[key];g['ns']+=r['ns'];g['calls']+=r['calls'];g['commands']+=1
result['sampled_command_groups_by_sum_worker_wall_time']=sorted([dict(label=k,ms=round(v['ns']/1e6,3),calls=v['calls'],samples=v['commands']) for k,v in grouped.items()],key=lambda x:x['ms'],reverse=True)[:24]
cache={k:sum(w[k] for workers in navigation for w in workers) for k in ('cache_lookups','cache_hits','cache_misses','cache_uncached','ready_rows','sampled_rows','duplicate_rows','allocated_bytes','cache_mutex_ns','cache_grid_ns')}
result['sampled_cache_navigation']=cache
if (run/'trace.csv.input.csv').exists():
    inputs=list(csv.DictReader((run/'trace.csv.input.csv').open()))
    result['input_dispatch_ms']=stats([int(r['dispatch_ns'])/1e6 for r in inputs])
    result['input_sdl_event_ms']=stats([int(r['event_ns'])/1e6 for r in inputs])
    result['input_first100_event_ms']=stats([int(r['event_ns'])/1e6 for r in inputs[:100]])
    result['input_last100_event_ms']=stats([int(r['event_ns'])/1e6 for r in inputs[-100:]])
if (run/'memory.json').exists():
    memory=json.loads((run/'memory.json').read_text())
    end=memory[-1]['seconds'] if memory else 0
    capture_seconds=(max(r['at_ns'] for r in rows)-origin)/1e9
    begin=min(max(20,capture_seconds+20),max(20,end-120))
    steady=[r for r in memory if begin<=r['seconds']<=end-20 and 'VmRSS' in r]
    early=[r['VmRSS']/1024 for r in steady if r['seconds']<begin+100]
    late=[r['VmRSS']/1024 for r in steady if r['seconds']>end-120]
    result['memory']=dict(samples=len(memory),duration_seconds=end,steady_begin_seconds=round(begin,2),peak_rss_kib=max(r.get('VmRSS',0) for r in memory),peak_hwm_kib=max(r.get('VmHWM',0) for r in memory),steady_early_rss_mib=stats(early),steady_late_rss_mib=stats(late))
(run/'summary.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))

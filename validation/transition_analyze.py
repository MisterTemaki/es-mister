"""Separate first redraw from logical SystemView transition completion (slot 0)."""
import argparse,csv,json,math,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('runs',nargs='+');p.add_argument('--output',required=True);p.add_argument('--require-complete',action='store_true');a=p.parse_args()
def stats(v):
    v=sorted(v)
    if not v:return {'n':0}
    return dict(n=len(v),median=round(statistics.median(v),3),p95=round(v[math.ceil(.95*len(v))-1],3),p99=round(v[math.ceil(.99*len(v))-1],3),max=round(v[-1],3))
results=[]
for name in a.runs:
    root=pathlib.Path(name)
    frames=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((root/'trace.csv').open()) if r['worker']=='0']
    assert frames and 'system_animation_ms' in frames[0],'New phase tracer required'
    inputs=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((root/'trace.csv.input.csv').open())]
    queued=[r['at_ns']-r['event_ns'] for r in inputs]
    dispatched=[r['at_ns']-r['dispatch_ns'] for r in inputs]
    rows=[];intervals=[]
    for i,start in enumerate(queued):
        # The queued timestamp includes SDL's integer-ms queue estimate. It can
        # lie just before the previous idle frame and pick the old selection.
        # Use exact dequeue time to associate frames, while retaining queue time
        # as the latency origin. The next dequeue bounds interruption.
        limit=dispatched[i+1] if i+1<len(dispatched) else frames[-1]['at_ns']+1
        group=[f for f in frames if dispatched[i]<=f['at_ns']<limit and f['system_cursor']>=0]
        cursor=group[0]['system_cursor'] if group else -1
        active=next((f for f in group if f['system_cursor']==cursor and f['system_animation_ms']>=0),None)
        end=next((f for f in group if active and f['at_ns']>active['at_ns'] and f['system_cursor']==cursor and f['system_animation_ms']<0),None)
        end_ns=max(end['at_ns'],end['finished_ns']) if end else None
        rows.append(dict(command=i,cursor=cursor,first_redraw_ms=inputs[i]['event_ns']/1e6,
            transition_end_ms=(end_ns-start)/1e6 if end else None,active_sample=bool(active)))
        if active:
            drawn=[f['finished_ns'] for f in group if f['at_ns']>=active['at_ns'] and (not end or f['at_ns']<=end['at_ns']) and f['redraw'] and f['finished_ns']]
            intervals.extend((b-a)/1e6 for a,b in zip(drawn,drawn[1:]))
    result=dict(run=root.name,commands=len(rows),completed=sum(r['transition_end_ms'] is not None for r in rows),
        active_without_end=sum(r['active_sample'] and r['transition_end_ms'] is None for r in rows),
        first_redraw_ms=stats([r['first_redraw_ms'] for r in rows]),
        main_transition_end_ms=stats([r['transition_end_ms'] for r in rows if r['transition_end_ms'] is not None]),
        redraw_intervals_in_transition_ms=stats(intervals),intervals_above_16_7_ms=sum(v>1000/60 for v in intervals))
    if a.require_complete:assert result['completed']==len(inputs),'Unobserved/interrupted transition in quiet scenario: '+root.name
    (root/'transition-commands.json').write_text(json.dumps(rows,indent=2));results.append(result);print(json.dumps(result))
pathlib.Path(a.output).write_text(json.dumps(dict(definition='SDL queued event to first frame with SystemView animation slot 0 inactive after an active sample; same cursor; does not include info fade slot 2 or TV latency',runs=results),indent=2))

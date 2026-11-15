"""Analyze first input after fixed idle and short/held directional controls."""
import argparse,bisect,csv,json,math,pathlib,re,statistics
p=argparse.ArgumentParser();p.add_argument('mode',choices=('idle','controls'));p.add_argument('runs',nargs='+');p.add_argument('--output',required=True);a=p.parse_args()
def stats(values):
    s=sorted(values)
    return dict(n=len(s),median=round(statistics.median(s),3),p95=round(s[math.ceil(.95*len(s))-1],3),p99=round(s[math.ceil(.99*len(s))-1],3),max=round(s[-1],3)) if s else dict(n=0)
def rows(path):return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
reports=[]
for name in a.runs:
    d=pathlib.Path(name);c=json.loads((d/'configuration.json').read_text());memory=json.loads((d/'memory.json').read_text())
    assert not (d/'framebuffer-collision.json').exists()
    assert max(r['framebuffer_owners'] for r in memory)==1
    assert c['framebuffer_size']=='1920,1080'
    inputs=rows(d/'trace.csv.input.csv');frames=rows(d/'trace.csv')
    text=(d/'startup.log').read_text()
    probes=re.findall(r'^\s*([\d.]+) \[\d+\] input selection result: (\d+) -> (\d+)',text,re.M)
    moves=[(int(x),int(y)) for at,x,y in probes]
    report=dict(run=d.name,binary=c['binary_sha256'],responses=len(inputs),rss_peak_mib=round(max(r.get('VmRSS',0) for r in memory)/1024,3),
        sdl_first_redraw_ms=stats([r['event_ns']/1e6 for r in inputs]),dispatch_first_redraw_ms=stats([r['dispatch_ns']/1e6 for r in inputs]))
    if a.mode=='idle':
        response_frames=sorted((r for r in frames if r['worker']==0),key=lambda r:r['at_ns']);times=[r['at_ns'] for r in response_frames]
        matched=[response_frames[bisect.bisect_right(times,event['at_ns'])-1] for event in inputs]
        report['first_response_renderer_ms']=stats([r['execute_ns']/1e6 for r in matched])
        report['first_response_partial_frames']=sum(r['partial'] for r in matched)
        injections=json.loads((d/'injections.json').read_text());assert len(injections)==len(inputs)==8
        assert len(moves)==8 and all(x!=y for x,y in moves),'An idle key did not change selection'
        assert all(14.99e9<=r['idle_ns']<=15.5e9 for r in injections),'Idle interval was not fixed'
        report['uinput_first_redraw_ms']=stats([(event['at_ns']-injection['inject_ns'])/1e6 for event,injection in zip(inputs,injections)])
        process=json.loads((d/'process.json').read_text());samples=process['samples'];cpu=[];rss=[]
        for injection in injections:
            selected=[r for r in samples if injection['inject_ns']-12e9<=r['at_ns']<=injection['inject_ns']-2e9]
            if len(selected)<2:continue
            first,last=selected[0],selected[-1]
            ticks=last['user_ticks']+last['system_ticks']-first['user_ticks']-first['system_ticks']
            cpu.append(100*ticks/process['clock_ticks']/((last['at_ns']-first['at_ns'])/1e9))
            rss.extend(r['rss_pages']*process['page_bytes']/1048576 for r in selected)
        report['static_idle_cpu_percent_one_core_units']=stats(cpu)
        report['static_idle_rss_mib']=stats(rss)
        report['selection_changes']=moves
    else:
        # The unmatched probes can be paired with later redraws by StartupTrace.
        # Their pending-event duration is not a useful visual response metric.
        report.pop('sdl_first_redraw_ms');report.pop('dispatch_first_redraw_ms')
        report['pending_event_records']=report.pop('responses')
        assert len(moves)==6 and inputs,'Expected six selection probes: short/F24/hold/F24/hold/F24'
        assert moves[0][1]==moves[0][0]+1,'Short right did not advance exactly once'
        assert moves[1][0]==moves[1][1]==moves[0][1],'Extra advance after short release'
        assert moves[2][0]==moves[1][1] and moves[2][1]==moves[2][0]+1
        assert moves[3][0]==moves[3][1] and moves[3][0]>moves[2][1],'Held right did not repeat'
        assert moves[4][0]==moves[3][1] and moves[4][1]==moves[4][0]-1
        assert moves[5][0]==moves[5][1] and moves[5][0]<moves[4][1],'Held left did not repeat'
        # F24 is deliberately unmapped: it probes selection without a visual
        # change. It need not produce a redraw and can remain the pending SDL
        # press until the next useful action. Never require six visual answers.
        offset=inputs[0]['at_ns']-inputs[0]['dispatch_ns']-float(probes[0][0])*1e9
        probe_times=[offset+float(at)*1e9 for at,x,y in probes]
        for start_probe,end_probe,release_ms in ((0,1,60),(2,3,1200),(4,5,1200)):
            begin=probe_times[start_probe]+(release_ms+300)*1e6;finish=probe_times[end_probe]
            cursors={r['system_cursor'] for r in frames if r['worker']==0 and begin<=r['at_ns']<finish and r['system_cursor']>=0}
            assert not cursors or cursors=={moves[end_probe][1]},'Cursor continued after release'
        report.update(selection_probes=moves,short_advance=1,held_right_advance=moves[3][1]-moves[2][0],held_left_advance=moves[4][0]-moves[5][1],release_stable=True,
            note='Six selection probes; F24 is unmapped and not required to redraw. This control check does not measure six useful visual-response latencies.')
    reports.append(report);print(json.dumps(report),flush=True)
pathlib.Path(a.output).write_text(json.dumps(dict(runs=reports,notes=['Small-sample p99 can equal the maximum; continuous 1000-command tests provide the sustained tail estimate.','uinput timestamps are software injection, not physical controller/TV photons.','Idle CPU is sampled [-12,-2] seconds before each injection, after the previous animation. CPU100% denotes one of two cores.']),indent=2))

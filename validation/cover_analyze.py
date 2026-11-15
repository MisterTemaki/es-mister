"""Analyze isolated cover tests; distinguish committed buffers, RSS and diagnostics."""
import argparse,collections,csv,json,math,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('runs',nargs='+');p.add_argument('--output',required=True);a=p.parse_args()
def stats(values):
    s=sorted(values)
    return dict(n=len(s),median=round(statistics.median(s),3),p95=round(s[math.ceil(.95*len(s))-1],3),p99=round(s[math.ceil(.99*len(s))-1],3),max=round(s[-1],3)) if s else dict(n=0)
def csvrows(path):return list(csv.DictReader(path.open())) if path.exists() else []
reports=[]
for name in a.runs:
    d=pathlib.Path(name);c=json.loads((d/'configuration.json').read_text());env=c['renderer_environment']
    assert c['cover_fixture'] and c['framebuffer_size']=='1920,1080'
    assert not (d/'framebuffer-collision.json').exists(),'Multiple framebuffer owners'
    memory=json.loads((d/'memory.json').read_text())
    assert memory and max(r.get('framebuffer_owners',0) for r in memory)==1,'Owner guard missing or invalid'
    process=json.loads((d/'process.json').read_text());samples=process['samples']
    def cpu(first,last):
        wall=(last['at_ns']-first['at_ns'])/1e9
        ticks=last['user_ticks']+last['system_ticks']-first['user_ticks']-first['system_ticks']
        return round(100*ticks/process['clock_ticks']/wall,3) if wall>0 else None
    # The last samples can include texture/GUI destruction; never call those idle RSS.
    end=samples[-1]['at_ns'];idle=[r for r in samples if end-20e9<=r['at_ns']<=end-5e9]
    inputs=csvrows(d/'trace.csv.input.csv');frames=csvrows(d/'trace.csv');textures=csvrows(d/'textures.csv')
    report=dict(run=d.name,binary_sha256=c['binary_sha256'],texture_threads=env.get('ES_TEXTURE_THREADS'),
        fixture_view=c.get('fixture_view') or 'copied production preference',
        frame_trace_enabled='ES_SOFT_TRACE' in env,texture_trace_enabled='ES_TEXTURE_TRACE' in env,
        sampled_duration_s=memory[-1]['seconds'],rss_peak_mib=round(max(r.get('VmRSS',0) for r in memory)/1024,3),
        hwm_peak_mib=round(max(r.get('VmHWM',0) for r in memory)/1024,3),
        script_key_commands=sum(l.startswith('key ') for l in (d/'navigation.txt').read_text().splitlines()),
        answered_inputs=len(inputs),sdl_first_redraw_ms=stats([int(r['event_ns'])/1e6 for r in inputs]),
        cpu_percent_one_core_units=cpu(samples[0],samples[-1]),
        late_idle_cpu_percent_one_core_units=cpu(idle[0],idle[-1]) if len(idle)>1 else None,
        late_idle_rss_mib=stats([r['rss_pages']*process['page_bytes']/1048576 for r in idle]),
        major_faults=samples[-1]['major_faults']-samples[0]['major_faults'])
    commands=(d/'navigation.txt').read_text().splitlines()
    cycles=sum(l.startswith('log fixture-cycle-') for l in commands)
    if cycles and len(inputs)==report['script_key_commands'] and len(inputs)%cycles==0:
        per_cycle=len(inputs)//cycles;windows=[]
        for index in range(cycles):
            first=inputs[index*per_cycle];begin=int(first['at_ns'])-int(first['event_ns'])
            if index+1<cycles:
                following=inputs[(index+1)*per_cycle];finish=int(following['at_ns'])-int(following['event_ns'])
            else:finish=end-5e9
            observed=[s for s in samples if begin<=s['at_ns']<finish]
            texture_window=[r for r in textures if begin<=int(r['at_ns'])<finish]
            window=dict(cycle=index,process_samples=len(observed),
                rss_mib=stats([s['rss_pages']*process['page_bytes']/1048576 for s in observed]),
                cpu_percent_one_core_units=cpu(observed[0],observed[-1]) if len(observed)>1 else None)
            if texture_window:
                window['committed_peak_mib']=round(max(int(r['allocated_bytes']) for r in texture_window)/1048576,3)
                window['loader_queue_peak']=max(int(r['queue_including_active']) for r in texture_window)
            windows.append(window)
        report['cycle_windows']=windows
        report['cycle_window_definition']='First SDL injection in each scripted cycle to the next; final five seconds excluded. RSS sampled at 1 Hz; cycles include list/carousel/menu, not a single identical visual state.'
    f=[{k:int(v) for k,v in r.items()} for r in frames if r['worker']=='0' and r['redraw']=='1']
    if f:
        assert len({r['frame'] for r in f})<8192,'Trace buffer saturated'
        work=[sum(r[k] for k in ('input_ns','update_ns','record_ns','dirty_ns','execute_ns','post_ns'))/1e6 for r in f]
        report.update(drawn_frame_work_ms=stats(work),drawn_frames=len(f),drawn_above_16_7_ms=sum(v>1000/60 for v in work),
                      full_renderer_ms=stats([r['execute_ns']/1e6 for r in f if not r['partial']]))
    if textures:
        report['texture_samples']=len(textures)
        report['texture_sample_limit_reached']=len(textures)>=4096
        report['texture_last']={k:int(v) for k,v in textures[-1].items()}
        report['texture_peaks']={k:max(int(r[k]) for r in textures) for k in ('allocated_bytes','vram_bytes','decoded_ram_bytes','managed_entries','queue_including_active','resident_textures','decoded_textures','peak_active_loads')}
        events=csvrows(d/'textures.csv.events.csv');report['texture_events']=len(events)
        report['texture_event_limit_reached']=len(events)>=4096
        cover_events=[r for r in events if '/cover-fixture/images/' in r['path']]
        report['sampled_cover_load_wall_ms']=stats([int(r['elapsed_ns'])/1e6 for r in cover_events if r['kind']=='L'])
        pending={};availability=[]
        directional=[];navigation_requests=0
        keys=[l.split()[1] for l in commands if l.startswith('key ')]
        first_inject=int(inputs[0]['at_ns'])-int(inputs[0]['event_ns']) if inputs else None
        exit_index=next((i for i,k in enumerate(keys) if k=='Escape'),len(keys))
        exit_inject=(int(inputs[exit_index]['at_ns'])-int(inputs[exit_index]['event_ns'])) if inputs and exit_index<len(inputs) else None
        for r in cover_events:
            if r['kind']=='I':
                pending[r['path']]=int(r['at_ns'])
                if first_inject is not None and exit_inject is not None and first_inject<=int(r['at_ns'])<exit_inject:navigation_requests+=1
            elif r['kind']=='U' and r['path'] in pending:
                request=pending.pop(r['path']);elapsed=(int(r['at_ns'])-request)/1e6;availability.append(elapsed)
                if first_inject is not None and exit_inject is not None and first_inject<=request<=int(r['at_ns'])<exit_inject:directional.append(elapsed)
        report['sampled_request_to_upload_ms']=stats(availability)
        report['visible_list_request_to_upload_ms']=stats(directional)
        report['visible_list_requests']=navigation_requests
        report['visible_list_window']='First key injection until the first Escape injection; excludes startup and later hidden-list intervals. Upload is still a software readiness proxy.'
        report['availability_limitations']='Observed request/upload pairs only; paths may have multiple sizes. Not a photon or proven selected-cover latency. Events may be censored. Repeated path loads are not automatically eviction reloads.'
    reports.append(report);print(json.dumps(report),flush=True)
pathlib.Path(a.output).write_text(json.dumps(dict(runs=reports,notes=['CPU 100% means one core; device has two.','Memory counters describe committed texture buffers, not RSS/global process budget.','Idle window excludes final five seconds to avoid shutdown memory release.','Major faults can be file-backed faults without swap.']),indent=2))

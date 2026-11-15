import argparse,csv,json,math,pathlib,re,statistics
p=argparse.ArgumentParser();p.add_argument('runs',nargs='+');p.add_argument('--summary-output');p.add_argument('--group-by',choices=('system_workers','sample_neon','upload_neon','alpha_scan'),default='system_workers');args=p.parse_args()
groups={}
for name in args.runs:
    run=pathlib.Path(name)
    launch=json.loads((run/'launcher-timeline.json').read_text())
    inputs=list(csv.DictReader((run/'trace.csv.input.csv').open()))
    injections=json.loads((run/'injections.json').read_text())
    if len(inputs)!=1 or len(injections)!=1:raise RuntimeError('Expected exactly one first action: '+str(run))
    end=int(inputs[0]['at_ns']);inject=injections[0]['inject_ns']
    text=(run/('startup.log' if (run/'startup.log').exists() else 'es-startup.log')).read_text()
    def mark(phrase):
        found=re.search(r'^\s*([\d.]+) \[\d+\] '+re.escape(phrase)+r'\s*$',text,re.M)
        return float(found.group(1)) if found else None
    loading=re.search(r'< load systems \(([\d.]+) s\)',text)
    result=dict(run=run.name,spawn_to_first_input_ms=(end-launch['spawn_begin_ns'])/1e6,
        probe_begin_to_first_input_ms=(end-launch['launch_begin_ns'])/1e6,
        helper_ms=(launch['helper_end_ns']-launch['helper_begin_ns'])/1e6,
        sdl_input_ms=int(inputs[0]['event_ns'])/1e6,uinput_ms=(end-inject)/1e6,
        first_splash_s=mark('first frame shown'),ready_s=mark('ready'),
        first_interactive_s=mark('first interactive frame shown'),
        load_systems_s=float(loading.group(1)) if loading else None)
    frames=list(csv.DictReader((run/'trace.csv').open()))
    response=max((r for r in frames if int(r['at_ns'])<=end),key=lambda r:int(r['at_ns']))
    for key in ('ui_ns','input_ns','update_ns','record_ns','execute_ns','event_wait_ns'):
        result['first_input_'+key.removesuffix('_ns')+'_ms']=int(response[key])/1e6
    config=json.loads((run/'configuration.json').read_text())
    result['wrapper_to_first_input_ms']=(end-launch['wrapper_spawn_ns'])/1e6 if launch.get('wrapper_spawn_ns') else None
    result['process_start_precision_ns']=launch.get('process_start_precision_ns')
    result['boot_id']=config.get('boot_id')
    result['elf_read_before_spawn']=config.get('elf_read_before_spawn',True)
    if config.get('wrapper'):
        events=[line.split(maxsplit=1) for line in (run/'wrapper.log').read_text().splitlines()]
        marks={event:float(at) for at,event in events}
        for field,begin,finish in [('wrapper_fixed_wait_ms','fixed_wait_begin','fixed_wait_end'),('wrapper_menu_wait_ms','menu_wait_begin',None)]:
            if finish is None:finish=next(event for at,event in events if event.startswith('menu_wait_end'))
            result[field]=(marks[finish]-marks[begin])*1000
        result['wrapper_begin_to_process_ms']=(marks['process_begin']-marks['wrapper_begin'])*1000
    selection=re.search(r'input selection result: (\d+) -> (\d+)',text)
    result['selection_before']=int(selection.group(1)) if selection else None
    result['selection_after']=int(selection.group(2)) if selection else None
    if config['renderer_environment'].get('ES_STARTUP_INPUT_PROBE') and (not selection or selection.group(1)==selection.group(2)):
        raise RuntimeError('First key did not change the system selection: '+str(run))
    result['system_workers']=config['renderer_environment'].get('ES_SYSTEM_THREADS','default')
    result['sample_neon']=config['renderer_environment'].get('ES_SOFT_SAMPLE_NEON','0')
    result['upload_neon']=config['renderer_environment'].get('ES_SOFT_UPLOAD_NEON','0')
    result['alpha_scan']=config['renderer_environment'].get('ES_SOFT_ALPHA_SCAN','0')
    (run/'startup-summary.json').write_text(json.dumps(result,indent=2))
    groups.setdefault(result[args.group_by],[]).append(result)
    print(json.dumps(result))
def stats(a):
    a=sorted(a)
    return dict(n=len(a),median=round(statistics.median(a),3),p95=round(a[math.ceil(.95*len(a))-1],3),min=round(a[0],3),max=round(a[-1],3))
fields=('spawn_to_first_input_ms','helper_ms','sdl_input_ms','uinput_ms','first_splash_s','ready_s','first_interactive_s','load_systems_s','first_input_ui_ms','first_input_record_ms','first_input_update_ms','first_input_execute_ms','wrapper_to_first_input_ms','wrapper_fixed_wait_ms','wrapper_menu_wait_ms','wrapper_begin_to_process_ms')
summary={k:{field:stats([r[field] for r in rows if r.get(field) is not None]) for field in fields if any(r.get(field) is not None for r in rows)} for k,rows in groups.items()}
print(json.dumps(summary,indent=2))
if args.summary_output:pathlib.Path(args.summary_output).write_text(json.dumps(summary,indent=2))

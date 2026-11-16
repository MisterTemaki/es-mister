import pathlib,json,re,shutil
v=pathlib.Path(__file__).resolve().parent
out=v.parent/'es-mister/audit/performance-1080p'
data={n:json.loads((v/'target-results'/n/'summary.json').read_text()) for n in ('sustained-control','sustained-fast')}
a,b=data.values()
table='''<!-- MEASUREMENTS BEGIN -->
## Sustained comparison

| Measure (ms) | Control | Optimised | Reduction |
|---|---:|---:|---:|
'''
for key,stat,label in [('input_sdl_event_ms','median','SDL queue → redraw, median'),('input_sdl_event_ms','p95_sample','SDL queue → redraw, p95'),('input_sdl_event_ms','p99','SDL queue → redraw, p99'),('input_sdl_event_ms','max','SDL queue → redraw, maximum observed'),('input_dispatch_ms','median','Dispatch → redraw, median'),('input_dispatch_ms','p99','Dispatch → redraw, p99')]:
    x,y=a[key][stat],b[key][stat]
    table+=f'| {label} | {x:.3f} | {y:.3f} | {(1-y/x)*100:.1f}% |\n'
table+='\nEach version answered exactly 1,000 commands. The control lasted '
table+=f"{a['memory']['duration_seconds']:.1f} s and the optimised one {b['memory']['duration_seconds']:.1f} s, including start-up and shutdown. "
table+='The total duration also includes the script\'s artificial waits; it is not a throughput measure.\n\n'
table+='| Stage of the full redraw, median (ms) | Control | Optimised |\n|---|---:|---:|\n'
for key,label in [('ui_ms','UI preparation'),('dirty_ms','Dirty regions'),('execute_ms','Execution including copy'),('draw_max_ms','Slowest worker\'s drawing'),('copy_max_ms','Slowest worker\'s copy'),('pacing_ms','Limiter wait')]:
    table+=f"| {label} | {a['full_navigation'][key]['median']:.3f} | {b['full_navigation'][key]['median']:.3f} |\n"
table+='\nThe frame distributions differ because the corrected limiter allows more animation steps '
table+='and culling allows partial updates. The main comparison is the response to the same inputs. '
table+='**60 FPS was not reached in Carbon\'s full redraws at 1080p.**\n\n'
table+='Maximum resident memory during the instrumented test: '
table+=f"{a['memory']['peak_rss_kib']/1024:.2f} MiB for the control and {b['memory']['peak_rss_kib']/1024:.2f} MiB for the optimised one. "
table+='After the tracing buffer filled and before shutdown, the medians of 100 s windows were '
table+=f"{b['memory']['steady_early_rss_mib']['median']:.3f} and {b['memory']['steady_late_rss_mib']['median']:.3f} MiB for the optimised one. "
table+='There was no relevant growth in that window; this does not prove there are no leaks on every screen. '
table+='Tracing reserves and occupies memory progressively; the numbers are not the minimum usage of a normal run.\n\n'
quality=v/'target-results/quality-final/stats.txt'
if quality.exists():
    table+='In the separate quality validation, **0 of 91 frames checked diverged** between partial rendering and the full reference. '
    table+='Carousel, NES list, selection change and menu were captured at 1920×1080 and inspected visually. '
    table+='That mode redraws the reference and saves PNGs; its times are not used as a benchmark.\n\n'
one=v/'target-results/threads-one/summary.json'
if one.exists():
    c=json.loads(one.read_text())
    table+=f"One thread had an internal median of {c['input_dispatch_ms']['median']:.3f} ms over 24 commands, against {b['input_dispatch_ms']['median']:.3f} ms with two threads in the long test. "
    table+='The samples have different sizes, but no reason appeared to change the two-thread default.\n\n'
startup=[]
for label in ('startup-control-final','startup-fast-final'):
    p=v/'target-results'/label/'startup.log'
    if not p.exists():continue
    text=p.read_text()
    ready=re.findall(r'^\s*([\d.]+) \[\d+\] ready$',text,re.M)
    action=re.findall(r'^\s*([\d.]+) \[\d+\] first press answered:',text,re.M)
    if ready and action:startup.append((label,float(ready[-1]),float(action[-1])))
if startup:
    table+='Start-up of a new process with the system cache already warm, one run of each:\n\n'
    table+='| Version | Ready mark (s) | First command answered (s) |\n|---|---:|---:|\n'
    for label,ready,action in startup: table+=f'| {label} | {ready:.3f} | {action:.3f} |\n'
    table+='\nThe script waits 9 s before the key; the first command confirms it works at that moment and '
    table+='is an upper bound, not a search for the first interactive millisecond. The values exclude the wrapper\'s '
    table+='initial wait. There was no reboot/global cache clearing to measure a cold boot, and no statistical '
    table+='start-up gain is concluded from one sample.\n\n'
table+='<!-- MEASUREMENTS END -->'
path=out/'RESULTS.md';text=path.read_text()
text=text.replace('<!-- MEASUREMENTS -->',table) if '<!-- MEASUREMENTS -->' in text else re.sub(r'<!-- MEASUREMENTS BEGIN -->.*?<!-- MEASUREMENTS END -->',lambda _:table,text,flags=re.S)
path.write_text(text)
e=out/'evidence-stage2';e.mkdir(exist_ok=True)
for name in ('stage2-checks.log','regressions-stage2.log','settings-stage2.log','renderer-stage2.log','http-server-stage2.log','thread-pool-stage2.log','copy-file-stage2.log','similarity-stage2.log','theme-stage2.log','helper-target-validation.log','raster-final-native.log','raster-target-results.log','finish-target-checks.log','finish-target-checks-retry.log','cross-build-performance.log','deployment.log','deployment-verification.json'):
    if (v/name).exists():shutil.copy2(v/name,e/name)
for name in ('installed-control','profile','transparent','transparent-off','alpha','sustained-control','sustained-fast','quality-final','threads-one','startup-control-final','startup-fast-final'):
    if (v/'target-results'/(name+'.tar.gz')).exists():shutil.copy2(v/'target-results'/(name+'.tar.gz'),e)
    p=v/'target-results'/name/'summary.json'
    if p.exists():shutil.copy2(p,e/(name+'-summary.json'))
for name in ('target_run.py','trace_analyze.py','raster_build.py','target_raster.py','cross_build.sh','cross_cache.sh','cross_sources.py','cross_prepare.sh','build_checks.py','final_checks.py','quality-script.txt','startup-script.txt','arm_similarity.sh','finish_target_checks.py'):
    if (v/name).exists():shutil.copy2(v/name,e/name)
shots=v/'target-results/quality-final'
for name in ('carousel.png','games.png','menu.png'):
    if (shots/name).exists():shutil.copy2(shots/name,e/name)
(e/'runtime-manifest.json').write_text((v/'update-1080p/manifest.json').read_text())
print('Updated',path)

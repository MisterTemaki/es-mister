"""Locate measured cover-tail events in the scripted view, without rerunning tests."""
import csv,json,pathlib
V=pathlib.Path(__file__).resolve().parent
reports=[]
for d in sorted((V/'target-results').glob('covers-source-final-v1-*')):
    if not d.is_dir():continue
    keys=[line.split()[1] for line in (d/'navigation.txt').read_text().splitlines() if line.startswith('key ')]
    inputs=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((d/'trace.csv.input.csv').open())]
    frames=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((d/'trace.csv').open()) if row['worker']=='0']
    assert len(keys)==len(inputs)==52
    slow=[]
    for index in sorted(range(len(inputs)),key=lambda i:-inputs[i]['event_ns'])[:5]:
        r=inputs[index];response=max((f for f in frames if f['at_ns']<=r['at_ns']),key=lambda f:f['at_ns'])
        phase='grid with covers: '+('down' if keys[index]=='Down' else 'back') if index<48 else ['list→carousel','open menu','close menu','carousel→list'][index-48]
        slow.append(dict(input_number=index+1,key=keys[index],phase=phase,sdl_ms=round(r['event_ns']/1e6,3),
            queue_ms=round((r['event_ns']-r['dispatch_ns'])/1e6,3),ui_ms=round(response['ui_ns']/1e6,3),renderer_ms=round(response['execute_ns']/1e6,3),partial=bool(response['partial'])))
    reports.append(dict(run=d.name,slowest_five=slow))
(V/'covers-tail-locations.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2))
for r in reports:print(json.dumps(r,ensure_ascii=False))

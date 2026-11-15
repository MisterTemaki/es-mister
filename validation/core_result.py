"""Summarize the original probe's existing return trace; quality and speed stay separate."""
import argparse,csv,json,pathlib,re
def summarize(d,quality):
    d=pathlib.Path(d);c=json.loads((d/'configuration.json').read_text())
    assert c['framebuffer_size']=='1920,1080' and c['binary_sha_verified_after_run']
    memory=json.loads((d/'memory.json').read_text());assert max(r['framebuffer_owners'] for r in memory)==1
    assert not (d/'framebuffer-collision.json').exists()
    checks=re.findall(r'(\d+) of (\d+) checked wrong',(d/'stats.txt').read_text())
    if quality:assert c['renderer_environment'].get('ES_SOFT_DIRTY_CHECK')=='1' and checks and all(int(x)==0 for x,n in checks)
    else:assert 'ES_SOFT_DIRTY_CHECK' not in c['renderer_environment'] and not checks
    assert not list(d.glob('*.png'))
    inputs=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((d/'trace.csv.input.csv').open())]
    keys=[line.split()[1] for line in (d/'navigation.txt').read_text().splitlines() if line.startswith('key ')]
    probes=[tuple(map(int,m)) for m in re.findall(r'input selection result: (\d+) -> (\d+)',(d/'startup.log').read_text())]
    legacy=keys==['Return','Escape','Return','Escape']
    if legacy:assert quality and len(inputs)==4,'Legacy control sequence is not a valid speed pass'
    else:
        assert keys==['Return','Escape','F24','Right','F24','Left','F24']
        assert len(probes)==5,'Post-core carousel was not reached'
        assert probes[0][0]==probes[0][1]
        assert probes[1][0]==probes[0][1] and probes[1][1]!=probes[1][0],'Right was ignored after return'
        assert probes[2][0]==probes[2][1]==probes[1][1]
        assert probes[3][0]==probes[2][1] and probes[3][1]==probes[0][1],'Left did not return to the previous selection'
        assert probes[4][0]==probes[4][1]==probes[3][1]
    markers=[line.split(maxsplit=1) for line in (d/'core-wrapper.log').read_text().splitlines()];marks={event:float(at) for at,event in markers}
    complete=next(event for at,event in markers if event.startswith('return_complete'))
    assert 'framebuffer=1920x1080' in complete
    helper_begin=next(event for at,event in markers if event.startswith('return_helper_begin'))
    helper_end=next(event for at,event in markers if event.startswith('return_helper_end') and 'result=ok' in event)
    assert sum(event=='launch_begin' for at,event in markers)==1,'Unexpected second core launch: invalidate this run'
    started=next(event for at,event in markers if event.startswith('core_started name=NES'))
    request_file=next((d/n for n in ('return-request.json','return-recovery.json') if (d/n).exists()),None)
    record=dict(core='NES',framebuffer='1920,1080',quality_mode=quality,quality_checked_frames=sum(int(n) for x,n in checks),quality_wrong_frames=sum(int(x) for x,n in checks),
        wrapper_marker_precision_ms=10,return_fixed_wait_ms=round((marks['return_fixed_wait_end']-marks['return_fixed_wait_begin'])*1000,3),
        return_helper_ms=round((marks[helper_end]-marks[helper_begin])*1000,3),post_return_input_records=len(inputs)-1,
        useful_control_actions_verified=not legacy,selection_probes=probes,physical_tv_audio_verified=False,
        note='Quality-mode times include double pixel checking and are not normal navigation performance.' if quality else 'One probe return; software timing, not physical TV latency or a production-catalog before/after gain.')
    record['control_probe_delay_note']='The script deliberately delays post-return controls; their timestamp does not estimate earliest input readiness. First-draw timing excludes that wait.'
    record['script_contains_settle_wait']='settle ' in (d/'navigation.txt').read_text()
    if request_file:
        request=json.loads(request_file.read_text());assert request['boot_id']==c['boot_id']
        offset=request['boot_to_monotonic_ns'];menu=round(marks['return_fixed_wait_begin']*1e9)+offset;ready=round(marks[complete]*1e9)+offset
        assert all(r['at_ns']>ready for r in inputs[1:])
        frames=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((d/'trace.csv').open()) if r['worker']=='0' and r['redraw']=='1']
        first=min(r['finished_ns'] for r in frames if r['finished_ns']>ready)
        record.update(return_request=request,nes_observed_to_return_request_s=round((request['return_request_ns']-offset)/1e9-marks[started],3),
            menu_observed_to_first_draw_ms=round((first-menu)/1e6,3),helper_complete_to_first_draw_ms=round((first-ready)/1e6,3),
            menu_observed_to_first_post_return_redraw_ms=round((inputs[1]['at_ns']-menu)/1e6,3))
    (d/'lifecycle-summary.json').write_text(json.dumps(record,indent=2));return record
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('--quality',action='store_true');a=p.parse_args();print(json.dumps(summarize(a.run,a.quality)))

"""Exercise real file replacement/backup/rollback in an owned host temp directory."""
import contextlib,hashlib,importlib.util,io,json,pathlib,tempfile,unittest.mock
V=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('installer',V.parent/'es-mister/mister/tools/install-update.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
expected={'emulationstation','es-mister-helper','es-mister.sh','mister-launch.sh','LEIA-ME.txt'}
def fixture(root):
    app=root/'app';package=root/'package';(package/'payload').mkdir(parents=True);app.mkdir()
    before={name:('original '+name+'\n').encode() for name in expected}
    before['es-mister.ini']=b'SIZE=auto\nFPS=30\nBENCH=1\nCUSTOM=preserved\n'
    for name,data in before.items():(app/name).write_bytes(data)
    manifest={}
    for name in expected:
        data=('candidate '+name+'\n').encode();(package/'payload'/name).write_bytes(data);manifest[name]=hashlib.sha256(data).hexdigest()
    return app,package,manifest,before
for scenario in ('success','readiness-failure','stop-failure'):
    with tempfile.TemporaryDirectory(prefix='es-installer-') as temp:
        app,package,manifest,before=fixture(pathlib.Path(temp));output=io.StringIO()
        ready=dict(frontend_pid=123,launcher_pid=122,api_ready=True,framebuffer='1920,1080')
        start_effect=[RuntimeError('injected readiness failure'),ready] if scenario=='readiness-failure' else [ready]
        stop_effect=RuntimeError('injected active frontend') if scenario=='stop-failure' else None
        with unittest.mock.patch.object(m,'stop',side_effect=stop_effect) as stop,unittest.mock.patch.object(m,'start_ready',side_effect=start_effect) as start,contextlib.redirect_stdout(output):
            try:m.install(package,manifest,app,expected)
            except RuntimeError:
                assert scenario!='success'
            else:assert scenario=='success'
        if scenario=='success':
            record=json.loads(output.getvalue());backup=pathlib.Path(record['backup'])
            for name,data in before.items():
                assert (backup/name).read_bytes()==data
                assert record['original_sha256'][name]==hashlib.sha256(data).hexdigest()
            for name in expected:assert (app/name).read_bytes()==(package/'payload'/name).read_bytes()
            ini=(app/'es-mister.ini').read_text();assert 'SIZE=1920x1080\n' in ini and 'FPS=60\n' in ini and 'CUSTOM=preserved\n' in ini and 'BENCH=0\n' in ini
            assert stop.call_count==1 and start.call_count==1
        else:
            for name,data in before.items():assert (app/name).read_bytes()==data,(scenario,name)
            assert start.call_count==(2 if scenario=='readiness-failure' else 0)
        assert not list(app.glob('.codex-stage-*')),'Owned staging was not cleaned'
        print(scenario,'real temporary files verified; no target connection')

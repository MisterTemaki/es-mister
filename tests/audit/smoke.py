import pathlib, tempfile, subprocess, os, shutil
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('binary',type=pathlib.Path)
parser.add_argument('--output-dir',type=pathlib.Path,default=pathlib.Path('audit-smoke'))
args=parser.parse_args()
base=args.output_dir.resolve()
base.mkdir(parents=True,exist_ok=True)
root=pathlib.Path(__file__).resolve().parents[2]
binary=args.binary.resolve()
resources=binary.parent/'resources'
failed=False
if not resources.exists(): resources.symlink_to(root/'resources',target_is_directory=True)
with tempfile.TemporaryDirectory(prefix='es-smoke-') as temp:
    work=pathlib.Path(temp)
    config=work/'.emulationstation'
    config.mkdir()
    games=work/'games'
    games.mkdir()
    (games/'Audit.nes').write_bytes(b'audit-fixture')
    (config/'es_systems.cfg').write_text('<systemList><system><name>nes</name><fullname>Audit NES</fullname>'
                                       '<path>'+str(games)+'</path><extension>.nes</extension>'
                                       '<command>/bin/true</command><platform>nes</platform><theme>nes</theme>'
                                       '</system></systemList>')
    (config/'es_settings.cfg').write_text('<config><bool name="SplashScreen" value="false"/>'
                                         '<bool name="BackgroundJoystickInput" value="false"/>'
                                         '<bool name="ThreadedLoading" value="false"/></config>')
    (config/'es_input.cfg').write_text('<inputList><inputConfig type="keyboard" deviceName="Keyboard" deviceGUID="-1">'
                                     '<input name="b" type="key" id="13" value="1"/>'
                                     '<input name="a" type="key" id="27" value="1"/>'
                                     '<input name="start" type="key" id="32" value="1"/>'
                                     '<input name="up" type="key" id="1073741906" value="1"/>'
                                     '<input name="down" type="key" id="1073741905" value="1"/>'
                                     '</inputConfig></inputList>')
    script=work/'smoke.txt'
    script.write_text('wait 500\nshot initial\nkey Return\nwait 300\nshot navigation\nquit\n')
    env=dict(os.environ, HOME=temp, SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
             ES_SOFT_OUTPUT='null',ES_SOFT_SIZE='640x360',ES_SOFT_FPS='30',
             ES_SOFT_SCRIPT=str(script),ES_SOFT_SHOTS=str(work))
    for name,args in [('help',['--help']),('startup',['--windowed','--no-splash','--resolution','640','360','--debug'])]:
        try:
            result=subprocess.run([str(binary),*args],cwd=root,env=env,stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT,text=True,timeout=40)
            (base/('smoke-'+name+'.log')).write_text(result.stdout)
            print(name,'exit',result.returncode,flush=True)
            failed |= result.returncode != 0
        except subprocess.TimeoutExpired as error:
            (base/('smoke-'+name+'.log')).write_bytes(error.stdout or b'')
            print(name,'TIMEOUT',flush=True)
            failed=True
    log=config/'es_log.txt'
    if log.exists(): shutil.copy2(log,base/'smoke-application.log')
    for image in work.glob('*.png'): shutil.copy2(image,base/('smoke-'+image.name))

failed |= not (base/'smoke-navigation.png').exists()
failed |= not (base/'smoke-application.log').exists() or 'cleanly shutting down' not in (base/'smoke-application.log').read_text()
raise SystemExit(bool(failed))

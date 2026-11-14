import pathlib,subprocess,sys,os
v=pathlib.Path(__file__).resolve().parent
src=v.parent/'es-mister/tests/audit'
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
jobs=[('regressions-stage2',['python3',str(src/'run.py'),str(v/'regressions/audit_regressions')]),
      ('settings-stage2',['python3',str(v/'settings_check.py'),'stage2']),
      ('renderer-stage2',['python3',str(src/'run_renderer.py'),str(v/'regressions/audit_renderer')]),
      ('http-server-stage2',[str(v/'regressions/audit_http_server')]),
      ('thread-pool-stage2',[str(v/'regressions/audit_thread_pool')]),
      ('copy-file-stage2',[str(v/'regressions/audit_copy_file')]),
      ('similarity-stage2',[str(v/'regressions/audit_similarity')]),
      ('theme-stage2',['python3',str(src/'run_theme.py'),str(v/'regressions/audit_theme')])]
failed=[]
for name,cmd in jobs:
    try:
        result=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
        text=result.stdout;code=result.returncode
    except subprocess.TimeoutExpired as e:text=str(e);code=-1
    (v/(name+'.log')).write_text(text)
    print(name,code,text[-2000:],flush=True)
    if code:failed.append(name)
sys.exit(bool(failed))

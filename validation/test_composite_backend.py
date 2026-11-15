import pathlib,subprocess,os
v=pathlib.Path(__file__).resolve().parent;reference=None
for mode in ('0','1'):
    p=subprocess.run([str(v/'regressions/audit_renderer')],env=dict(os.environ,ES_SOFT_COMPOSITE=mode,ES_SOFT_OPTIMIZATIONS='1',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
    print('COMPOSITE',mode,'EXIT',p.returncode,p.stdout,flush=True)
    assert p.returncode==0
    if reference is None:reference=p.stdout
    else:assert reference==p.stdout,'Backend pixels changed'
print('Composite exact alpha/translation/fallback/backend lifetime checks passed',flush=True)

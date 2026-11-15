import pathlib, subprocess, tempfile, os, sys
base=pathlib.Path(__file__).resolve().parent
label=sys.argv[1] if len(sys.argv)>1 else 'final'
with tempfile.TemporaryDirectory(prefix='es-settings-') as folder:
    result=subprocess.run([str(base/'regressions/audit_settings')],
                          env=dict(os.environ,ES_TEST_HOME=folder,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
(base/('settings-'+label+'.log')).write_text(result.stdout)
print(result.stdout,end='')
raise SystemExit(result.returncode)

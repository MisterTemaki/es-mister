import os,pathlib,subprocess
v=pathlib.Path(__file__).resolve().parent
reference=None
for tile in (0,7,17,256):
    env=dict(os.environ,ES_SOFT_TILE_X=str(tile),ES_SOFT_OPTIMIZATIONS='1',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    p=subprocess.run([str(v/'regressions/audit_renderer')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=60)
    print('TILE',tile,'EXIT',p.returncode,p.stdout,flush=True)
    assert p.returncode==0
    if reference is None:reference=p.stdout
    else:assert p.stdout==reference,'Tile subdivision changed pixels'
print('All tile boundaries preserve backend pixels',flush=True)

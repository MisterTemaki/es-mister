import os,pathlib,subprocess,sys
binary=pathlib.Path(sys.argv[1]).resolve()
results=[]
for mode in ('0','1'):
    env=dict(os.environ,ES_SOFT_OPTIMIZATIONS=mode,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    for key in ('ES_SOFT_SCRIPT','ES_SOFT_TRACE','ES_SOFT_DUMP','ES_SOFT_BENCH','ES_SOFT_DIRTY_CHECK'):env.pop(key,None)
    result=subprocess.run([str(binary)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    print('optimizations',mode,'exit',result.returncode,flush=True)
    print(result.stdout,flush=True)
    if result.returncode:raise SystemExit(result.returncode)
    results.append(result.stdout)
assert results[0]==results[1],'FRAME PIXEL HASH MISMATCH'
print('Backend: transparent-only frames, empty swaps, mixed alpha, additive blend and stencil preserve output; ASan/UBSan/LSan passed.')

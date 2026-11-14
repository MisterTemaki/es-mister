import os,pathlib,subprocess,sys,tempfile,zipfile
binary=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='es-theme-test-') as folder:
    root=pathlib.Path(folder)
    valid=root/'valid.zip'
    with zipfile.ZipFile(valid,'w',compression=zipfile.ZIP_STORED) as z:z.writestr('repo-main/theme.xml',b'new-theme')
    bad=root/'bad.zip';bad.write_bytes(valid.read_bytes().replace(b'new-theme',b'bad-theme',1))
    traversal=root/'traversal.zip'
    with zipfile.ZipFile(traversal,'w') as z:z.writestr('../escaped',b'unsafe')
    install=root/'install';install.mkdir()
    env=dict(os.environ,ES_TEST_HOME=folder,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(binary),str(valid),str(bad),str(traversal),str(install)],env=env,check=True,timeout=30)
    assert not (root/'escaped').exists()

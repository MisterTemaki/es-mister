import pathlib, subprocess, zipfile, hashlib, shutil, json, tempfile, collections
base=pathlib.Path(__file__).resolve().parent
workspace=base.parent
root=workspace/'es-mister'
evidence=root/'audit/evidencias'
evidence.mkdir(parents=True,exist_ok=True)

# Keep the smoke runner portable in the delivered source tree.
smoke=(base/'smoke.py').read_text()
old="base=pathlib.Path(__file__).resolve().parent\nroot=base.parent/'es-mister'\nbinary=base/'native/emulationstation'\nresources=base/'native/resources'"
new="""import argparse
parser=argparse.ArgumentParser()
parser.add_argument('binary',type=pathlib.Path)
parser.add_argument('--output-dir',type=pathlib.Path,default=pathlib.Path('audit-smoke'))
args=parser.parse_args()
base=args.output_dir.resolve()
base.mkdir(parents=True,exist_ok=True)
root=pathlib.Path(__file__).resolve().parents[2]
binary=args.binary.resolve()
resources=binary.parent/'resources'
failed=False"""
assert old in smoke
smoke=smoke.replace(old,new)
smoke=smoke.replace("print(name,'exit',result.returncode,flush=True)","print(name,'exit',result.returncode,flush=True)\n            failed |= result.returncode != 0")
smoke=smoke.replace("print(name,'TIMEOUT',flush=True)","print(name,'TIMEOUT',flush=True)\n            failed=True")
smoke += "\nfailed |= not (base/'smoke-navigation.png').exists()\nfailed |= not (base/'smoke-application.log').exists() or 'cleanly shutting down' not in (base/'smoke-application.log').read_text()\nraise SystemExit(bool(failed))\n"
(root/'tests/audit/smoke.py').write_text(smoke)

for name in ('regressions-baseline.log','regressions-final.log','settings-baseline.log','settings-final.log',
             'smoke-help.log','smoke-startup.log','smoke-application.log','smoke-initial.png','smoke-navigation.png',
             'cppcheck.stderr','cppcheck.stdout','cppcheck-final.xml','cppcheck-final.stdout','shellcheck.stdout'):
    shutil.copy2(base/name,evidence/name)
shutil.copy2(workspace/'baseline-configure.log',evidence/'baseline-configure.log')

checks=[]
for path in sorted(root.rglob('*.sh')):
    if '.git' in path.parts: continue
    result=subprocess.run(['bash','-n',str(path)],capture_output=True,text=True)
    checks.append({'file':path.relative_to(root).as_posix(),'exit':result.returncode,'stderr':result.stderr})
    if result.returncode: raise RuntimeError(result.stderr)
for path in root.rglob('*.py'):
    if '.git' in path.parts: continue
    compile(path.read_bytes(),str(path),'exec')
(evidence/'sintaxe.json').write_text(json.dumps(checks,indent=2))

binary=base/'native/emulationstation'
environment={
    'date':'2026-11-13','host':'Ubuntu 26.04 / WSL2 / Windows',
    'baseline_git_local':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
    'cmake':subprocess.check_output(['cmake','--version'],text=True).splitlines()[0],
    'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
    'cppcheck':subprocess.check_output([str(base/'tools/usr/bin/cppcheck'),'--version'],text=True).strip(),
    'binary':subprocess.check_output(['file',str(binary)],text=True).strip(),
    'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
    'native_build_exit':0,
    'sanitizers':'address, undefined; leak detection enabled; halt_on_error enabled',
    'flags':['MISTER=ON','CEC=OFF','USE_SYSTEM_PUGIXML=ON','CMAKE_BUILD_TYPE=Debug','CMAKE_POLICY_VERSION_MINIMUM=3.5'],
    'hardware_tested':False,
}
(evidence/'ambiente.json').write_text(json.dumps(environment,ensure_ascii=False,indent=2))
(evidence/'ldd.txt').write_text(subprocess.check_output(['ldd',str(binary)],text=True))

subprocess.run(['git','diff','--check'],cwd=root,check=True)
subprocess.run(['git','add','-N','tests','es-core/src/utils/Xml.h'],cwd=root,check=True)
patch=workspace/'es-mister-auditoria-etapa1.patch'
with patch.open('wb') as file:
    subprocess.run(['git','diff','--binary','--','.',':(exclude)audit'],cwd=root,stdout=file,check=True)
with tempfile.TemporaryDirectory(prefix='es-patch-check-') as temp:
    with zipfile.ZipFile('/mnt/d/FPGA/Frontend/Mistercera/es-mister-src-5b5c7bc.zip') as original:
        original.extractall(temp)
    check=subprocess.run(['git','apply','--check',str(patch)],cwd=pathlib.Path(temp)/'es-mister',capture_output=True,text=True)
    (evidence/'patch-check.json').write_text(json.dumps({'exit':check.returncode,'stdout':check.stdout,'stderr':check.stderr},indent=2))
    if check.returncode: raise RuntimeError(check.stderr)

changed=subprocess.check_output(['git','diff','--name-only','--','.',':(exclude)audit'],cwd=root,text=True)
(root/'audit/arquivos-alterados.txt').write_text(changed)
archive=workspace/'es-mister-5b5c7bc-auditoria-etapa1.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as output:
    for path in sorted(root.rglob('*')):
        if not path.is_file() or '.git' in path.parts or '__pycache__' in path.parts or path.suffix=='.pyc': continue
        info=zipfile.ZipInfo.from_file(path,arcname='es-mister/'+path.relative_to(root).as_posix())
        info.compress_type=zipfile.ZIP_DEFLATED
        info.external_attr=(0o100755 if path.suffix in ('.sh','.py') else 0o100644)<<16
        output.writestr(info,path.read_bytes())
for artifact in (archive,patch):
    digest=hashlib.sha256(artifact.read_bytes()).hexdigest()
    artifact.with_name(artifact.name+'.sha256').write_text(digest+'  '+artifact.name+'\n')
    print(artifact.name,artifact.stat().st_size,digest,flush=True)
with zipfile.ZipFile(archive) as output:
    bad=output.testzip()
    assert bad is None,bad
    print('Archive CRC verified;',len(output.infolist()),'files; forward patch verified',flush=True)

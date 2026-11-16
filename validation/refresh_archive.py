import pathlib,zipfile,os,hashlib,shutil
base=pathlib.Path(__file__).resolve().parent
workspace=base.parent
root=workspace/'es-mister'
archive=workspace/'es-mister-5b5c7bc-auditoria-etapa1.zip'
temporary=workspace/'es-mister-5b5c7bc-auditoria-etapa1.refresh.zip'
updates=['audit/REPORT.md','audit/evidence/regressions-final.log']
shutil.copy2(base/'regressions-final.log',root/'audit/evidence/regressions-final.log')
assert '17/17 cases passed' in (root/'audit/evidence/regressions-final.log').read_text()
with zipfile.ZipFile(archive) as source,zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as output:
    for info in source.infolist():
        relative=info.filename.removeprefix('es-mister/')
        data=(root/relative).read_bytes() if relative in updates else source.read(info)
        output.writestr(info,data)
with zipfile.ZipFile(temporary) as checked:
    assert checked.testzip() is None
    assert len(checked.namelist())==len(set(checked.namelist()))
    for relative in updates:
        assert checked.read('es-mister/'+relative)==(root/relative).read_bytes()
os.replace(temporary,archive)
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_name(archive.name+'.sha256').write_text(digest+'  '+archive.name+'\n')
print(archive.name,archive.stat().st_size,digest)
print('Final report and 17-test evidence verified inside archive')

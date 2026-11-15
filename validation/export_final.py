import pathlib,subprocess,zipfile,hashlib,json,shutil,io
v=pathlib.Path(__file__).resolve().parent
base=v.parent;root=base/'es-mister'
def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
head=git('rev-parse','HEAD').decode().strip()
assert not git('status','--porcelain','--ignore-submodules=all').strip(),'Commit or inspect source changes before exporting'
archive=base/'es-mister-1080p-fontes-20261114.zip'
subprocess.run(['git','-C',str(root),'archive','--format=zip','--prefix=es-mister/','-o',str(archive),'HEAD'],check=True)
module=root/'external/pugixml'
module_head=subprocess.check_output(['git','-C',str(module),'rev-parse','HEAD']).decode().strip()
assert module_head=='61c944895e071872370219cadade8423dc6e04dc'
assert module_head in git('ls-tree','HEAD','external/pugixml').decode()
# Export the pinned objects to avoid Windows/WSL autocrlf differences in the working tree.
module_zip=subprocess.check_output(['git','-C',str(module),'archive','--format=zip','--prefix=es-mister/external/pugixml/',module_head])
with zipfile.ZipFile(archive,'a',zipfile.ZIP_DEFLATED) as z:
    with zipfile.ZipFile(io.BytesIO(module_zip)) as dependency:
        for item in dependency.infolist():
            if not item.is_dir():z.writestr(item,dependency.read(item))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert 'es-mister/external/pugixml/src/pugixml.cpp' in z.namelist()
    assert not any('/.git/' in name or name.endswith('/.git') for name in z.namelist())
patch=base/'es-mister-1080p-correcoes-20261114.patch'
patch.write_bytes(git('diff','--binary','bd16a02',head))
files=[archive,patch,base/'es-mister-1080p-atualizacao-20261113.zip']
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(base/'es-mister-1080p-SHA256SUMS.txt').write_text(''.join(sha+'  '+name+'\n' for name,sha in hashes.items()))
manifest=dict(commit=head,original_zip_sha256='f87ee31502c598e3e6e5c526df0a2e770a8d7bf79ca9fb967ad329826669863a',artifacts=hashes,
              installed=json.loads((v/'deployment-verification.json').read_text()))
(base/'es-mister-1080p-entrega.json').write_text(json.dumps(manifest,indent=2))
for p in files:print(p.name,p.stat().st_size,hashes[p.name])
print('Source commit',head,'pugixml commit',module_head)

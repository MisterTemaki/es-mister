import hashlib,io,json,pathlib,tarfile,urllib.request,shutil
work=pathlib.Path('/mnt/es-mister-codex-arm')
ref='297637bc0a0f00dc504dbc1a47ebdaa577f8ae19'
dest=work/('src/rapidjson-'+ref)
if not dest.exists():
    data=urllib.request.urlopen('https://codeload.github.com/Tencent/rapidjson/tar.gz/'+ref,timeout=60).read()
    dest.mkdir()
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as tar:
        for member in tar.getmembers():
            parts=pathlib.PurePosixPath(member.name).parts[1:]
            if not parts:continue
            member.name=str(pathlib.PurePosixPath(*parts));tar.extract(member,dest,filter='data')
    (work/'rapidjson-manifest.json').write_text(json.dumps(dict(commit=ref,sha256=hashlib.sha256(data).hexdigest()),indent=2))
shutil.copytree(dest/'include/rapidjson',work/'prefix/include/rapidjson',dirs_exist_ok=True)
print('RapidJSON pinned:',ref,flush=True)

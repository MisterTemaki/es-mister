"""Prepare isolated pinned release sources when WSL cannot connect to github.com Git."""
import hashlib, io, json, pathlib, subprocess, tarfile, urllib.request
work = pathlib.Path('/mnt/es-mister-codex-arm')
refs = [
    ('freetype', 'freetype/freetype', 'VER-2-14-3'),
    ('alsa-lib', 'alsa-project/alsa-lib', 'v1.2.16.1'),
    ('SDL2', 'libsdl-org/SDL', 'release-2.32.10'),
    ('SDL2_mixer', 'libsdl-org/SDL_mixer', 'release-2.8.2'),
    ('curl', 'curl/curl', 'curl-8_19_0'),
    ('rapidjson', 'Tencent/rapidjson', '297637bc0a0f00dc504dbc1a47ebdaa577f8ae19'),
    ('vlc', 'videolan/vlc', '3.0.21'),
]
manifest_path=work/'source-manifest.json'
manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else []
for name, repo, ref in refs:
    dest = work/'src'/name
    if dest.exists(): continue
    url = f'https://codeload.github.com/{repo}/tar.gz/{ref}'
    print('Fetching', name, ref, flush=True)
    data = urllib.request.urlopen(url, timeout=60).read()
    dest.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tar:
        for member in tar.getmembers():
            relative = pathlib.PurePosixPath(member.name).parts[1:]
            if not relative: continue
            member.name = str(pathlib.PurePosixPath(*relative))
            tar.extract(member, dest, filter='data')
    manifest.append(dict(name=name, ref=ref, url=url, sha256=hashlib.sha256(data).hexdigest()))
    if name == 'SDL2':
        subprocess.run(['git', 'init', '-q', str(dest)], check=True)
        subprocess.run(['git', '-C', str(dest), 'add', '.'], check=True)
manifest_path.write_text(json.dumps(manifest, indent=2))

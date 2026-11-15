import pathlib,shutil,hashlib,json,zipfile
v=pathlib.Path(__file__).resolve().parent
root=v.parent/'es-mister'
package=v/'update-1080p'
payload=package/'payload';payload.mkdir(parents=True,exist_ok=True)
files={'emulationstation':v/'arm/emulationstation-performance-stripped',
       'es-mister-helper':v/'arm/es-mister-helper',
       'es-mister.sh':root/'mister/scripts/es-mister.sh',
       'LEIA-ME.txt':root/'mister/LEIA-ME.txt'}
manifest={}
for name,source in files.items():
    data=source.read_bytes().replace(b'\r\n',b'\n') if name.endswith(('.sh','.txt')) else source.read_bytes()
    (payload/name).write_bytes(data)
    manifest[name]=hashlib.sha256(data).hexdigest()
(package/'manifest.json').write_text(json.dumps(manifest,indent=2))
shutil.copy2(root/'mister/tools/install-update.py',package/'install-update.py')
(package/'INSTALAR.txt').write_text('''Atualização MiSTer em 1080p — 13/11/2026

O aparelho 192.168.15.14 já recebeu esta atualização quando indicado no relatório de implantação.
Para reaplicar: extraia esta pasta em /media/fat/codex-update-1080p, volte ao MENU do MiSTer e execute:
  python3 /media/fat/codex-update-1080p/install-update.py --check
  python3 /media/fat/codex-update-1080p/install-update.py

Requer Python 3 no MiSTer. Não copie o payload sobre o executável em uso.
O instalador valida os hashes, encerra a interface pela API local, prepara arquivos completos,
faz backup em /media/fat/emulationstation/backups/codex-1080p-DATA e reinicia o launcher.
Mantém temas, jogos, controles e metadados. Ajusta SIZE=auto, FPS=60, STRIP=8, PLD=256,
BENCH=0, PROFILE=0, SLOW_DUMPS=0. FPS é limite, não desempenho garantido em transições completas.

Restauração: encerre a interface pela API /quit. Copie os cinco arquivos do backup indicado
no installation.json para /media/fat/emulationstation e inicie bash /media/fat/emulationstation/es-mister.sh.
Uma falha de energia durante atualização não equivale às falhas de operação testadas.
''',encoding='utf8')
dest=v.parent/'es-mister-1080p-atualizacao-20261113.zip'
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED) as z:
    for path in sorted(package.rglob('*')):
        if path.is_file():z.write(path,'codex-update-1080p/'+path.relative_to(package).as_posix())
print(dest,hashlib.sha256(dest.read_bytes()).hexdigest())
print(json.dumps(manifest,indent=2))

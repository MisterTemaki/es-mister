import pathlib, subprocess, json, collections, shutil
base = pathlib.Path(__file__).resolve().parent
root = base.parent / 'es-mister'
shutil.copytree(base/'tools/usr/lib/x86_64-linux-gnu/cppcheck/cfg', base/'tools/usr/bin/cfg', dirs_exist_ok=True)
files = sorted(p for p in root.rglob('*') if p.is_file() and '.git' not in p.parts)
items = []
for path in files:
    rel = path.relative_to(root).as_posix()
    data = path.read_bytes()
    items.append(dict(path=rel, bytes=len(data), lines=data.count(b'\n'), status='Não analisado'))
(base / 'inventory.json').write_text(json.dumps(items, indent=2, ensure_ascii=False), encoding='utf8')
print('Inventory:', len(items), 'files;', sum(i['lines'] for i in items), 'lines', flush=True)
commands = [
    ('cppcheck', [str(base/'tools/usr/bin/cppcheck'), '--enable=warning,performance,portability',
     '--inconclusive', '--std=c++17', '--max-configs=1', '-j6', '--xml', '--xml-version=2',
     '--inline-suppr', '-I'+str(root/'es-core/src'), '-I/usr/include/SDL2',
     '-I'+str(base/'tools/usr/include'), '-i'+str(root/'es-core/src/utils/zip_file.hpp'),
     str(root/'es-app/src'), str(root/'es-core/src'), str(root/'mister')]),
    ('shellcheck', [str(base/'tools/usr/bin/shellcheck'), '--format=json'] +
     [str(p) for p in files if p.suffix == '.sh']),
]
for name, command in commands:
    with (base/(name+'.stdout')).open('w') as out, (base/(name+'.stderr')).open('w') as err:
        result = subprocess.run(command, stdout=out, stderr=err, cwd=root)
    print(name, 'exit', result.returncode, flush=True)

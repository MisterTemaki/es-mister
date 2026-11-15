"""Deterministic isolated test catalog; no real ROM, save or scraped artwork."""
import hashlib,json,pathlib,struct,tarfile,zlib
v=pathlib.Path(__file__).resolve().parent
root=v/'cover-fixture';root.mkdir(exist_ok=True)
def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
width,height=512,768
images=root/'images';images.mkdir(exist_ok=True)
for seed in range(160):
    dest=images/f'cover-{seed:03}.png'
    if dest.exists():continue
    raw=bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            # Bands and fine detail exercise filtering while keeping fixture I/O bounded.
            raw.extend(((x*3+y+seed*17)%256,(x+y*5+seed*31)%256,((x//8)^(y//8)^seed)*7%256,255))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,3))+chunk(b'IEND',b'')
    dest.write_bytes(png)
rows=['<gameList>']
for n in range(1000):
    name=f'Fixture {n:04}'
    (root/f'{name}.nes').write_bytes(b'CODEX PERFORMANCE FIXTURE - NOT A ROM\n')
    rows.append(f'<game><path>./{name}.nes</path><name>{name}</name><image>./images/cover-{n%160:03}.png</image><desc>Deterministic performance fixture {n:04}; not playable.</desc></game>')
rows.append('</gameList>');(root/'gamelist.xml').write_text('\n'.join(rows))
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()}
(v/'cover-fixture-manifest.json').write_text(json.dumps(manifest,indent=2))
with tarfile.open(v/'cover-fixture.tar','w') as t:t.add(root,arcname='cover-fixture')
print('Fixture:',len(manifest),'files,160 covers,1000 entries;',sum(p.stat().st_size for p in root.rglob('*') if p.is_file()),'bytes')

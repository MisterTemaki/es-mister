"""Archive the rejected hypothesis and restore only findClearRuns from verified CBA."""
import hashlib,pathlib,sys
audit=pathlib.Path(__file__).resolve().parent.parent;root=audit/'es-mister'
sys.path.insert(0,str(root/'tests/audit'));from reference_source import read_reference
soft=root/'es-core/src/renderers/soft';archive=root/'tests/audit/experiments/alpha-scan'
archive.mkdir(parents=True,exist_ok=True)
for name in ('SoftRaster.cpp','SoftRaster.h','SoftAlphaScan.h'):
    data=(soft/name).read_bytes();dest=archive/name
    assert not dest.exists() or dest.read_bytes()==data,'Archive already differs'
    dest.write_bytes(data)
    print(name,hashlib.sha256(data).hexdigest())
def region(code):
    begin=code.index('void findClearRuns(Texture& texture)');start=code.index('{',begin);end=start+1;depth=1
    while depth:
        depth+=(code[end]=='{')-(code[end]=='}');end+=1
    return begin,end
path=soft/'SoftRaster.cpp';current=path.read_text();old=read_reference('es-core/src/renderers/soft/SoftRaster.cpp').decode()
begin,end=region(current);a,b=region(old)
assert 'ES_SOFT_ALPHA_SCAN' in current[begin:end]
current=(current[:begin]+old[a:b]+current[end:]).replace('#include "SoftAlphaScan.h"\n','')
assert 'ES_SOFT_ALPHA_SCAN' not in current
path.write_text(current)
(soft/'SoftAlphaScan.h').unlink()
(archive/'README.md').write_text('''# Rejected alpha-boundary scan hypothesis

These are experimental sources preserved only for reproducing the audit.
They do not enter mister/build.sh. The active findClearRuns was restored from
the verified cba2b66 reference. See ETAPA3.md for the on-device startup results.
The scan preserved pixels but did not demonstrate an application startup gain.
Build its differential microbenchmark with validation/build_clear.py.
''')

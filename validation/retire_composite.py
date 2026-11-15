"""Remove only our rejected, archived renderer experiment; retain the independent packed kernel."""
import pathlib,subprocess
v=pathlib.Path(__file__).resolve().parent;r=v.parent/'es-mister'
def baseline(path):return subprocess.check_output(['git','-C',str(r),'show','cba2b66:'+path])
for path,marker in [('es-core/src/renderers/Renderer_Soft.cpp','bool compositeLayers'),('es-core/src/renderers/soft/SoftRaster.h','bool composite('),('es-core/src/renderers/soft/SoftTrace.h','compositeNs')]:
    p=r/path;assert marker in p.read_text();p.write_bytes(baseline(path))
p=r/'es-core/src/renderers/soft/SoftRaster.cpp';text=p.read_text()
start=text.index('\t\tbool Raster::composite(');end=text.index('\t\tvoid Raster::drawQuad(',start)
text=text[:start]+text[end:];text=text.replace('#include <array>\n','')
assert 'spanDirectPacked' in text and 'Raster::composite' not in text
p.write_text(text)
p=r/'tests/audit/raster_workload.cpp';text=p.read_text();start=text.index('#ifdef AUDIT_COMPOSITE');end=text.index('#endif',start)+len('#endif\n');p.write_text(text[:start]+text[end:])
print('Rejected composite archived and removed; current renderer change is packed NEON only')

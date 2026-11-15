import pathlib, xml.etree.ElementTree as ET, collections, json
base = pathlib.Path(__file__).resolve().parent
errors = ET.parse(base/'cppcheck.stderr').findall('./errors/error')
print('Cppcheck totals:', dict(collections.Counter(e.attrib['severity'] for e in errors)))
for error in errors:
    if error.attrib['severity'] == 'error' or error.attrib['id'] in ('uninitvar','uninitMemberVar','memleak','resourceLeak','mismatchAllocDealloc','arrayIndexOutOfBounds','bufferAccessOutOfBounds','returnDanglingLifetime','danglingLifetime','deallocuse','invalidFunctionArg'):
        loc=error.find('location')
        print(error.attrib['severity'], error.attrib['id'], loc.attrib if loc is not None else '', error.attrib['msg'])
print('Shellcheck totals:', dict(collections.Counter(e['level'] for e in json.loads((base/'shellcheck.stdout').read_text()))))
inventory=json.loads((base/'inventory.json').read_text())
for prefix in ('es-app/', 'es-core/', 'external/', 'mister/', 'locale/', 'resources/', 'CMake/'):
    items=[i for i in inventory if i['path'].startswith(prefix)]
    print(prefix, len(items), 'files',sum(i['lines'] for i in items),'lines')

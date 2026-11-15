import pathlib, subprocess
base=pathlib.Path(__file__).resolve().parent
root=base.parent/'es-mister'
command=[str(base/'tools/usr/bin/cppcheck'), '--project='+str(base/'native/compile_commands.json'),
         '--file-filter='+str(root/'es-app/src/*'), '--file-filter='+str(root/'es-core/src/*'),
         '--enable=warning,performance,portability', '--inconclusive', '-j6', '--xml', '--xml-version=2',
         '-D__BYTE_ORDER__=1234', '-D__ORDER_LITTLE_ENDIAN__=1234', '-D__x86_64__=1',
         '-I/usr/include/x86_64-linux-gnu']
with (base/'cppcheck-final.stdout').open('w') as out, (base/'cppcheck-final.xml').open('w') as err:
    result=subprocess.run(command,stdout=out,stderr=err)
print('Cppcheck final exit', result.returncode)

"""Compile generated remote payloads without SSH, spawning, writes or uploads."""
import ast,pathlib,sys
path=pathlib.Path(__file__).with_name('target_run.py')
sys.path.insert(0,str(path.parent))
tree=ast.parse(path.read_text())
replacement=ast.parse('def remote(code):\n compile(code,"<generated remote payload>","exec")\n print("payload compiled")\n').body[0]
for i,node in enumerate(tree.body):
    if isinstance(node,ast.FunctionDef) and node.name=='remote':tree.body[i]=replacement
module=compile(ast.fix_missing_locations(tree),str(path),'exec')
for options in ([],['--wrapper'],['--cold-start','--expected-binary-sha','0'*64],['--wrapper','--cold-start','--expected-binary-sha','0'*64]):
    sys.argv=[str(path),'run','dry-payload',*options]
    exec(module,{'__file__':str(path),'__name__':'__main__'})
for action in ('restore','status'):
    sys.argv=[str(path),action]+(['dry-payload'] if action=='status' else [])
    exec(module,{'__file__':str(path),'__name__':'__main__'})

from target_guard import PROCESS_GUARD
verification=ast.parse(path.with_name('verify_installed_final.py').read_text())
expression=next(n.value for n in verification.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='code' for t in n.targets))
code=eval(compile(ast.Expression(expression),'<verification template>','eval'),{'PROCESS_GUARD':PROCESS_GUARD,'record':{'installed_sha256':{}}})
parsed=ast.parse(code)
separators=[n.args[0].value for n in ast.walk(parsed) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='split' and n.args and isinstance(n.args[0],ast.Constant) and isinstance(n.args[0].value,bytes)]
assert separators and all(value==b'\0' for value in separators),'Process environment must split on NUL'
print('installed verification payload compiled; NUL delimiters verified')

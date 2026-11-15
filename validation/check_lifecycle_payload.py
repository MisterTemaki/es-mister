"""Compile lifecycle payloads and check the FIFO newline without touching MiSTer."""
import ast,pathlib,sys
path=pathlib.Path(__file__).with_name('target_lifecycle.py');sys.path.insert(0,str(path.parent))
tree=ast.parse(path.read_text())
return_code=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='RETURN_MENU' for t in n.targets)).replace('__RUN__',repr('/media/fat/emulationstation/codex-performance-20261113/dry-lifecycle'))
return_tree=ast.parse(return_code)
payload=next(n.value for n in return_tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='payload' for t in n.targets))
assert ast.literal_eval(payload)==b'load_core /media/fat/menu.rbf\n'
print('Core return payload compiled; exact FIFO newline verified')
for node in ast.walk(tree):
    if isinstance(node,ast.Call) and isinstance(node.func,ast.Name) and node.func.id=='remote' and node.args and isinstance(node.args[0],ast.Constant) and isinstance(node.args[0].value,str):
        code=node.args[0].value;parsed=ast.parse(code)
        if 'payload=' in code:
            data=next(n.value for n in parsed.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='payload' for t in n.targets))
            assert ast.literal_eval(data)==b'load_core /media/fat/menu.rbf\n'
class PayloadCompiled(Exception):pass
replacement=ast.parse('def remote(code,*args,**kwargs):\n compile(code,"<lifecycle payload>","exec")\n parsed=ast.parse(code)\n if "SIZE=" in code:\n  text=next(n.args[0].value for n in ast.walk(parsed) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=="write_text" and n.args and isinstance(n.args[0],ast.Constant) and isinstance(n.args[0].value,str) and n.args[0].value.startswith("SIZE="))\n  assert text=="SIZE=1920x1080\\n"\n raise PayloadCompiled()\n').body[0]
for i,node in enumerate(tree.body):
    if isinstance(node,ast.FunctionDef) and node.name=='remote':tree.body[i]=replacement
module=compile(ast.fix_missing_locations(tree),str(path),'exec')
for mode in ('reboot','core'):
    sys.argv=[str(path),mode,'dry-lifecycle']
    try:exec(module,{'__file__':str(path),'__name__':'__main__','PayloadCompiled':PayloadCompiled,'ast':ast})
    except PayloadCompiled:print(mode,'payload compiled without SSH or mutations')

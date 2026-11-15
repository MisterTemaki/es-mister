import pathlib, subprocess, sys
base = pathlib.Path(__file__).resolve().parent
root = base.parent/'es-mister'
def run(name, command):
    with (base/(name+'.log')).open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    print(name, 'exit', result.returncode, flush=True)
    lines = (base/(name+'.log')).read_text(errors='replace').splitlines()
    for line in lines:
        if line.startswith(('CASE ', 'PASS ', 'FAIL:', 'SUMMARY:', '0/')) or ' cases passed' in line:
            print(line, flush=True)
    if result.returncode and not any(line.startswith('CASE ') for line in lines):
        print('\n'.join(lines[-20:]), flush=True)
    return result.returncode
if len(sys.argv)>1 and sys.argv[1]=='baseline':
    run('regressions-baseline', ['python3', str(root/'tests/audit/run.py'), str(base/'audit_regressions-baseline')])
else:
    if run('regressions-build', ['cmake', '--build', str(base/'regressions'), '-j4']):
        raise SystemExit(1)
    raise SystemExit(run('regressions-final', ['python3', str(root/'tests/audit/run.py'), str(base/'regressions/audit_regressions')]))

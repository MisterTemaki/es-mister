"""Original-configuration measurements when CPUFreq exposes no active policies.

Run in WSL from the audit root. Optional --resume-first waits for an already
started kernel-nav-original-1 instead of launching it twice. No kernel mutation.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import time

BASE = pathlib.Path(__file__).resolve().parent
REMOTE = '/media/fat/emulationstation/codex-performance-20261113'
SSH = '/mnt/c/Windows/System32/OpenSSH/ssh.exe'
KEY = r'C:\Users\USER\.ssh\id_ed25519_mister'


def remote(code):
    p = subprocess.run([SSH, '-T', '-i', KEY, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8',
                        'root@192.168.15.14', 'python3 -'], input=code, text=True,
                       stdout=subprocess.PIPE, check=True)
    return p.stdout.strip()


def run(*args):
    subprocess.run([sys.executable, str(BASE / 'target_run.py'), *args], check=True)


def wait(label, timeout=1200):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            state = remote(f"""import pathlib,json
p=pathlib.Path({(REMOTE+'/'+label)!r});pid=(p/'pid').read_text().strip()
print(json.dumps(dict(running=pathlib.Path('/proc/'+pid).exists(),samples=(p/'kernel-samples.json').exists())))""")
        except subprocess.CalledProcessError:
            # Only this read-only poll is retried. Never repeat a launch/mutation
            # whose remote result is unknown after a transport failure.
            print('SSH status query failed; device benchmark continues independently', label, flush=True)
            time.sleep(5)
            continue
        state = json.loads(state)
        if not state['running'] and state['samples']:
            return
        time.sleep(20)
    raise RuntimeError('Benchmark timeout: ' + label)


def collect(label):
    run('collect', label)
    dest = BASE / 'target-results' / label
    with (dest / 'kernel-analysis.log').open('w') as log:
        subprocess.run([sys.executable, str(BASE.parent / 'es-mister/tests/audit/kernel_analyze.py'), str(dest)],
                       stdout=log, check=True)
    summary = json.loads((dest / 'kernel-summary.json').read_text())
    print(label, json.dumps({k: summary[k] for k in ('input_sdl_to_first_redraw_ms', 'full_renderer',
                                                   'between_first_and_last_input')}), flush=True)


def idle(label, trials, seconds):
    print('START', label, trials, seconds, flush=True)
    remote(f"""import pathlib,subprocess
p=pathlib.Path({(REMOTE+'/'+label)!r});p.mkdir(exist_ok=True)
assert not (p/'pid').exists(),'Use a new label'
out=(p/'keyboard.log').open('w')
proc=subprocess.Popen(['python3',{(REMOTE+'/bin/idle_keyboard')!r},str(p),'--trials',{str(trials)!r},'--idle',{str(seconds)!r}],
 stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
(p/'keyboard-pid').write_text(str(proc.pid))""")
    for _ in range(30):
        if remote(f"from pathlib import Path; print(Path({(REMOTE+'/'+label+'/keyboard-ready')!r}).exists())") == 'True':
            break
        time.sleep(.1)
    else:
        raise RuntimeError('Keyboard not ready; SDL must not start without it')
    safety = BASE / 'kernel-idle-safety.txt'
    safety.write_text('wait 1200000\nquit\n')
    run('run', label, '--installed', '--script', str(safety), '--detail', '0', '--kernel-probe')
    wait(label, timeout=120 + trials * (seconds + 4))
    collect(label)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resume-first', action='store_true')
    parser.add_argument('--resume-after-idle', action='store_true')
    args = parser.parse_args()
    try:
        if not args.resume_first and not args.resume_after_idle:
            run('upload', str(BASE.parent / 'es-mister/tests/audit/kernel_probe.py'), 'kernel_probe')
            run('upload', str(BASE.parent / 'es-mister/tests/audit/idle_keyboard.py'), 'idle_keyboard')
            run('run', 'kernel-nav-original-1', '--installed', '--steps', '1000', '--detail', '0', '--kernel-probe')
        if not args.resume_after_idle:
            wait('kernel-nav-original-1')
            collect('kernel-nav-original-1')
            idle('kernel-idle-pilot', 2, 3)
            idle('kernel-idle-original-1', 16, 15)
        else:
            wait('kernel-idle-original-1')
            collect('kernel-idle-original-1')
        # Repeat the original navigation after idle. This is a reproducibility
        # check, NOT a governor A/B: no policy exists to change on this kernel.
        print('START kernel-nav-original-2', flush=True)
        run('run', 'kernel-nav-original-2', '--installed', '--steps', '1000', '--detail', '0', '--kernel-probe')
        wait('kernel-nav-original-2')
        collect('kernel-nav-original-2')
    finally:
        # target_run refuses restore if an existing frontend still owns the display.
        run('restore')
    print('KERNEL ORIGINAL-CONFIGURATION MEASUREMENTS COMPLETE', flush=True)

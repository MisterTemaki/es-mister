"""Shared /proc owner guard for the MiSTer audit tools."""
PROCESS_GUARD = r'''
import pathlib,time,subprocess,urllib.request
def owners_and_launchers():
    installed=pathlib.Path('/media/fat/emulationstation/emulationstation')
    candidates=pathlib.Path('/media/fat/emulationstation/codex-performance-20261113/bin')
    wrapper='/media/fat/emulationstation/es-mister.sh'
    owners=[];launchers=[]
    for entry in pathlib.Path('/proc').iterdir():
        if not entry.name.isdigit():continue
        try:
            exe=(entry/'exe').resolve(strict=True)
            arguments=(entry/'cmdline').read_bytes().split(b'\0')
            if exe==installed or exe.parent==candidates:owners.append((int(entry.name),str(exe)))
            if any(arg==wrapper.encode() or (arg.startswith((str(candidates.parent)+'/').encode()) and arg.endswith(b'/es-mister.sh')) or (arg.startswith(b'/tmp/codex-perf-') and arg.endswith(b'/package/es-mister.sh')) for arg in arguments):launchers.append(int(entry.name))
        except (OSError,ProcessLookupError):continue
    return owners,launchers
def stop_production():
    deadline=time.monotonic()+60;requested=set();stable=0
    while time.monotonic()<deadline:
        owners,launchers=owners_and_launchers()
        assert all(exe=='/media/fat/emulationstation/emulationstation' for pid,exe in owners),'Candidate already active'
        if not owners and not launchers:
            stable+=1
            if stable>=2:return
        else:stable=0
        if owners and any(pid not in requested for pid,exe in owners):
            try:
                urllib.request.urlopen('http://127.0.0.1:1234/quit',timeout=2).read()
                requested.update(pid for pid,exe in owners)
            except OSError:pass
        time.sleep(.2)
    raise RuntimeError('Frontend/launcher did not stop; refusing another framebuffer owner')
def restored_ready():
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        owners,launchers=owners_and_launchers()
        assert all(exe=='/media/fat/emulationstation/emulationstation' for pid,exe in owners),'Candidate still active'
        assert len(owners)<=1,'Multiple installed frontend processes'
        if len(owners)==1:
            pid=owners[0][0]
            trace=pathlib.Path('/media/fat/emulationstation/logs/es-startup.log')
            text=trace.read_text(errors='replace') if trace.exists() else ''
            # Production CBA predates the interactive-frame marker. Its ready
            # marker plus a live API confirms the child exists; stop_production
            # independently waits for the complete wrapper to exit next time.
            if ('['+str(pid)+'] ready') in text:
                try:
                    urllib.request.urlopen('http://127.0.0.1:1234/',timeout=2).read(1)
                    return pid
                except OSError:pass
        time.sleep(.2)
    raise RuntimeError('Original launcher has not produced an interactive frontend')
'''
